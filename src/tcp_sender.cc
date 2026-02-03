#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// How many sequence numbers are outstanding?
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  uint64_t result = 0;
  for ( auto& os : outstanding_segments_ )
    result += os.segment.sequence_length();
  return result;
}

// How many consecutive retransmissions have happened?
uint64_t TCPSender::consecutive_retransmissions() const
{
  return retransmissions_nums_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  Reader& reader = this->reader();

  // 1. 如果出现错误，发送空的 TCPSenderMessage
  if ( reader.has_error() ) {
    debug( "TCPSender::push(): reader has error, sending RST\n" );
    TCPSenderMessage msg = make_empty_message();
    msg.RST = true;
    transmit( msg );
    return;
  }

  // 2. 计算可用窗口大小
  uint64_t effective_window = window_size_ > 0 ? window_size_ : 1;
  uint64_t flight = sequence_numbers_in_flight();
  uint64_t available_window = ( effective_window > flight ) ? ( effective_window - flight ) : 0;
  debug( "effective_window = {}, available_window = {}", effective_window, available_window );

  // 3. 如果有可用窗口，尝试发送数据
  while ( available_window > 0 ) {
    // 构造 TCPSenderMessage
    TCPSenderMessage msg;

    // 设置 SYN
    if ( next_abs_seqno == 0 ) {
      msg.SYN = true;
      available_window--;
    }

    // 设置 seqno
    msg.seqno = Wrap32::wrap( next_abs_seqno, isn_ );

    // 设置 payload
    size_t len = min( available_window, static_cast<uint64_t>( TCPConfig::MAX_PAYLOAD_SIZE ) );
    string_view view = reader.peek();
    len = min( len, view.size() );
    msg.payload = string( view.substr( 0, len ) );
    reader.pop( len );
    available_window -= len;

    // 设置 FIN
    if ( reader.is_finished() && available_window > 0 ) {
      if ( next_abs_seqno < reader.bytes_popped() + 1 + 1 ) {
        msg.FIN = true;
        available_window--;
      }
    }

    if ( msg.sequence_length() == 0 )
      break;

    debug( "TCPSender::push(): prepared message: SYN={}, seqno={}, payload_size={}, FIN={}, RST={}",
           msg.SYN,
           msg.seqno.unwrap( isn_, 0 ),
           msg.payload.size(),
           msg.FIN,
           msg.RST );

    // 将消息加入 OutStanding 队列
    outstanding_segments_.push_back( OutstandingSegment { msg, next_abs_seqno } );
    next_abs_seqno += msg.sequence_length();

    // 发送消息
    transmit( msg );

    // 启动重传计时器
    if ( !timer_.is_running() )
      timer_.start_timer();
  }

  (void)transmit;
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = Wrap32::wrap( next_abs_seqno, isn_ );
  if ( this->reader().has_error() )
    msg.RST = true;
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( msg.RST ) {
    this->reader().set_error();
    return;
  }

  // 1. 更新窗口大小
  // debug("TCPSender::receive(): received message: ackno={}, window_size={}",
  //       msg.ackno.has_value() ? to_string(msg.ackno.value().unwrap(isn_, 0)) : "null",
  //       msg.window_size);
  window_size_ = msg.window_size;

  // 2. 处理 ACK
  if ( msg.ackno.has_value() ) {
    Wrap32 ackno = msg.ackno.value();

    uint64_t abs_ackno = ackno.unwrap( isn_, next_abs_seqno );

    if ( abs_ackno > next_abs_seqno )
      return;

    if ( abs_ackno > last_ackno_ ) {
      last_ackno_ = abs_ackno;

      // 重置重传计时器以及重传次数
      RTO_ms_ = initial_RTO_ms_;
      retransmissions_nums_ = 0;

      // 移除已确认的数据段
      while ( !outstanding_segments_.empty() ) {
        auto& os = outstanding_segments_.front();
        uint64_t seg_end_seqno = os.abs_seqno + os.segment.sequence_length();
        // debug("TCPSender::receive(): checking outstanding segment: seqno={}, length={}, seg_end_seqno={},
        // abs_ackno={}",
        //       os.segment.seqno.unwrap(isn_, 0), os.segment.sequence_length(), seg_end_seqno, abs_ackno);
        if ( seg_end_seqno <= abs_ackno ) {
          // debug("TCPSender::receive(): acknowledged segment: seqno={}, length={}",
          //       os.segment.seqno.unwrap(isn_, 0), os.segment.sequence_length());
          outstanding_segments_.pop_front();
        } else
          break;
      }

      // 如果还有未确认的数据，重置计时器
      if ( sequence_numbers_in_flight() > 0 )
        timer_.reset_timer();
    }

    if ( outstanding_segments_.empty() )
      timer_.stop_timer();
  }

  (void)msg;
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  // debug("TCPSender::tick(): ms_since_last_tick = {}", ms_since_last_tick);

  if ( !timer_.is_running() )
    return;

  timer_.pass_time( ms_since_last_tick );

  if ( timer_.elapsed_time() >= RTO_ms_ ) {
    // 超时重传
    if ( !outstanding_segments_.empty() ) {
      auto& os = outstanding_segments_.front();
      transmit( os.segment );

      if ( window_size_ > 0 ) {
        RTO_ms_ *= 2;
        retransmissions_nums_++;
      }

      timer_.reset_timer();
    } else
      timer_.stop_timer();
  }
}

bool Timer::is_running() const
{
  return timer_running_;
}

void Timer::start_timer()
{
  timer_running_ = true;
  timer_elapsed_ = 0;
}

void Timer::stop_timer()
{
  timer_running_ = false;
  timer_elapsed_ = 0;
}

void Timer::reset_timer()
{
  timer_running_ = true;
  timer_elapsed_ = 0;
}

void Timer::pass_time( uint64_t ms )
{
  if ( timer_running_ )
    timer_elapsed_ += ms;
}

uint64_t Timer::elapsed_time() const
{
  return timer_elapsed_;
}