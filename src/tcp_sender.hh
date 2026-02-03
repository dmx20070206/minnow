#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <deque>
#include <functional>

struct OutstandingSegment
{
  TCPSenderMessage segment; // 段本身
  uint64_t abs_seqno;       // 绝对序列号
};

class Timer
{
public:
  bool is_running() const;

  // 启动计时器
  void start_timer();

  // 停止计时器
  void stop_timer();

  //  重置计时器
  void reset_timer();

  // 经过指定的毫秒数
  void pass_time( uint64_t ms );

  // 获取计时器已经运行的时间
  uint64_t elapsed_time() const;

private:
  bool timer_running_ = false; // 计时器是否在运行
  uint64_t timer_elapsed_ = 0; // 计时器已经运行的时间
};

class TCPSender
{
public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
  TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
    : input_( std::move( input ) ), isn_( isn ), initial_RTO_ms_( initial_RTO_ms )
  {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // How many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // How many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }

  ByteStream input_;
  Wrap32 isn_;

  uint64_t initial_RTO_ms_;
  uint64_t current_time_ms_ = 0;
  uint64_t RTO_ms_ = initial_RTO_ms_;
  uint64_t retransmissions_nums_ = 0;

  Timer timer_ {};

  uint16_t window_size_ = 1;
  uint64_t last_ackno_ = 0;
  uint64_t next_abs_seqno = 0;
  std::deque<OutstandingSegment> outstanding_segments_ {};
};
