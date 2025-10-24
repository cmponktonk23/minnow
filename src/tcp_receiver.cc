#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  // debug( "unimplemented receive() called" );
  if ( message.RST ) {
    reassembler_.reader().set_error();
    return;
  }

  if ( message.SYN ) {
    isn_ = message.seqno;
  }

  uint64_t stream_index = message.seqno.unwrap( *isn_, reassembler_.next_byte() ) - ( !message.SYN );
  reassembler_.insert( stream_index, message.payload, message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  // debug( "unimplemented send() called" );
  std::optional<Wrap32> ackno = std::nullopt;
  if ( isn_ ) {
    debug( "next_byte {} ", reassembler_.next_byte() );
    ackno = Wrap32::wrap( reassembler_.next_byte() + 1 + reassembler_.writer().is_closed(), *isn_ );
  }

  uint64_t wnd_size = reassembler_.writer().available_capacity();
  if ( wnd_size > 65535 ) {
    wnd_size = 65535;
  }

  return { ackno, static_cast<uint16_t>( wnd_size ), reassembler_.writer().has_error() };
}
