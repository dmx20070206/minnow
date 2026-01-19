#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Sender's message structure:
  // struct TCPSenderMessage
  // {
  //     Wrap32 seqno{0};
  //     bool SYN{};
  //     std::string payload{};
  //     bool FIN{};
  //     bool RST{};
  // };

  // 1. 如果有 RST 标志，直接将 reassembler 的 reader 设置为错误状态
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  // 2. 处理 SYN 标志，设置 zero_point_ (ISN)
  if ( message.SYN )
    zero_point_ = message.seqno;

  // 3. 如果 zero_point_ 未设置，说明还未收到 SYN，直接返回
  if ( !zero_point_.has_value() )
    return;

  // 4. 计算 checkpoint，用当前已推送的字节数加 1
  uint64_t checkpoint = reassembler_.writer().bytes_pushed() + 1;
  // 5. 计算绝对序列号
  uint64_t abs_seqno = message.seqno.unwrap( zero_point_.value(), checkpoint );
  // 6. 计算流索引
  uint64_t stream_index = message.SYN ? 0 : abs_seqno - 1;

  // 7. 如果没有 SYN 且绝对序列号为 0，说明这是一个无效的报文段，直接返回
  if ( !message.SYN && abs_seqno == 0 )
    return;

  // 8. 将有效载荷插入重组器
  reassembler_.insert( stream_index, message.payload, message.FIN );

  (void)message;
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Receiver's message structure:
  // struct TCPReceiverMessage
  // {
  //     std::optional<Wrap32> ackno{};
  //     uint16_t window_size{};
  //     bool RST{};
  // };

  TCPReceiverMessage message;

  // 1. 设置 RST 标志
  if ( reassembler_.reader().has_error() )
    message.RST = true;

  // 2. 计算窗口大小
  size_t capacity = reassembler_.writer().available_capacity();
  message.window_size = capacity > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>( capacity );

  // 3. 设置 ACK 号
  if ( zero_point_.has_value() ) {
    // 计算下一个期望的绝对序列号
    uint64_t next_abs_seqno = reassembler_.writer().bytes_pushed() + 1;
    // 如果流已经结束，FIN 也占用一个序列号
    if ( reassembler_.writer().is_closed() )
      next_abs_seqno += 1;
    // 将绝对序列号转换为 Wrap32 格式的 ACK 号
    message.ackno = Wrap32::wrap( next_abs_seqno, zero_point_.value() );
  }

  return message;
}
