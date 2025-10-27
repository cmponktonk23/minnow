#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// How many sequence numbers are outstanding?
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return {};
}

// How many consecutive retransmissions have happened?
uint64_t TCPSender::consecutive_retransmissions() const
{
  return {};
}

void TCPSender::push( const TransmitFunction& transmit )
{
  auto &reader = input_.reader();

  // SYN
  if ( first_msg_ ) {
    auto syn = make_empty_message();
    syn.seqno = isn_;
    syn.SYN = true;
    transmit( syn );
    // Record
    outstanding_.push_back({
      abs_seqno_++,
      "",
    });
    first_msg_ = false;
  }

  // Payload
  while ( 1 ) {
    // RST
    if ( reader.has_error() ) {
      auto rst = make_empty_message();
      rst.seqno = Wrap32::wrap( abs_seqno_++ , isn_ );
      rst.RST = true;
      transmit( rst );
      break;
    }

    string_view stream = reader.peek();
    uint64_t send_len = min( stream.size(), min( (size_t)rwnd_, TCPConfig::MAX_PAYLOAD_SIZE ) );
    
    if (send_len == 0) break;

    string payload { stream.substr( 0, send_len ) };
    auto segment = make_empty_message();
    segment.seqno = Wrap32::wrap( abs_seqno_ , isn_ );
    segment.payload = move( payload );
    transmit( segment );
    // Record
    outstanding_.push_back({
      abs_seqno_,
      move( segment.payload ),
    });
    abs_seqno_ += send_len;
    reader.pop( send_len );
  }

  // FIN
  if ( reader.is_finished() ) {
    auto fin = make_empty_message();
    fin.seqno = Wrap32::wrap( abs_seqno_ , isn_ );
    fin.FIN = true;
    transmit(fin);
    // Record
    outstanding_.push_back({
      abs_seqno_++,
      "",
    });
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage{
    Wrap32(0),
    false,
    "",
    false,
    false,
  };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if (!msg.ackno.has_value()) return;

  // Get abs ackno from ackno
  uint64_t ackno_ = msg.ackno.value().unwrap( isn_, abs_ackno_ );
  
  // Drop message.
  if ( ackno_ < abs_ackno_ ) {
    return;
  }

  // Update ackno
  abs_ackno_ = ackno_;

  // Update receiver window size
  // No less than 1
  rwnd_ = max( (uint16_t)1, msg.window_size );

  // Pop out outstanding segments
  while ( !outstanding_.empty() ) {
    auto it = outstanding_.begin();
    if ( abs_ackno_ >= it->first_index_ + it->data_.size() ) {
      outstanding_.erase(it);
    } else {
      break;
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  debug( "ms_since_last_tick {}", ms_since_last_tick );
  transmit( TCPSenderMessage{} );
}
