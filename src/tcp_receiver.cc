#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Bug: peer发来RST的时候要将reader.set_error
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( message.SYN ) {
    isn_ = message.seqno;
  }

  /**
   *              SYN       c      a  t  FIN
   * seqno       2^32-1   2^32-1   0  1   2
   * abs seqno     0        1      2  3   4
   * stream idx             0      1  2
   */
  
  // 计算stream idx的两种情况：
  // 1. 当前收到的包带SYN flag，此时abs seqno必定为0，则不减1
  // 2. 当前收到的包不带SYN flag，此时stream_idx = abs_seqno-1
  uint64_t stream_index = message.seqno.unwrap( *isn_, reassembler_.next_byte() ) - ( !message.SYN );
  reassembler_.insert( stream_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  std::optional<Wrap32> ackno = std::nullopt;
  
  if ( isn_ ) {
    // 需要用abs_seqno计算ackno，abs_seqno = SYN + stream_index + FIN
    ackno = Wrap32::wrap( reassembler_.next_byte() + 1 + reassembler_.writer().is_closed(), *isn_ );
  }

  uint64_t wnd_size = reassembler_.writer().available_capacity();
  
  // Bug: rwnd不能超过65535
  if ( wnd_size > 65535 ) {
    wnd_size = 65535;
  }

  // 这里在没收到SYN时只能返回 ack<none> + rwnd（有可能没收到SYN先收到了后面的包）
  return { ackno, static_cast<uint16_t>( wnd_size ), reassembler_.writer().has_error() };
}
