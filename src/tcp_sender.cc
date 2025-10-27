#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// How many sequence numbers are outstanding?
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return sequence_number_in_flight_;
}

// How many consecutive retransmissions have happened?
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  // SYN
  // if ( first_msg_ ) {
  //   auto syn = make_empty_message();
  //   syn.SYN = true;
  //   transmit( syn );
  //   // Record
  //   outstanding_.push_back({
  //     abs_seqno_++,
  //     "",
  //   });
  //   ++sequence_number_in_flight_;
  //   if ( !RTO_timer_start_ ) {
  //     RTO_timer_start_ = true;
  //     RTO_timer_ = 0;
  //     RTO_ms_ = initial_RTO_ms_;
  //   }
  //   first_msg_ = false;
  // }

  // Payload
  while ( !is_finished_ ) {
    // // RST
    // if ( reader().has_error() ) {
    //   auto rst = make_empty_message();
    //   transmit( rst );
    //   break;
    // }

    string_view stream = reader().peek();
    uint64_t limit = 0;
    if ( sequence_number_in_flight_ < max( rwnd_, (uint16_t)1 ) ) {
      limit = min( max( rwnd_, (uint16_t)1 ) - sequence_number_in_flight_, TCPConfig::MAX_PAYLOAD_SIZE + 2 );
    }

    if ( limit == 0 ) return;

    auto segment = make_empty_message();
    if ( first_msg_ ) {
      first_msg_ = false;
      segment.SYN = true;
      --limit;
    }
    string payload { stream.substr( 0, min( TCPConfig::MAX_PAYLOAD_SIZE, min( limit, stream.size() ) ) ) };
    reader().pop( payload.size() );
    limit -= payload.size();
    if ( reader().is_finished() && limit > 0 ) {
      segment.FIN = true;
      is_finished_ = true;
    }
    segment.payload = move( payload );
    auto seq_len = segment.sequence_length();
    if ( seq_len == 0 ) return;
    transmit( segment );
    // Record
    outstanding_.push_back({
      abs_seqno_,
      segment.SYN,
      segment.FIN,
      move( segment.payload ),
    });
    abs_seqno_ += seq_len;
    sequence_number_in_flight_ += seq_len;

    if ( !RTO_timer_start_ ) {
      RTO_timer_start_ = true;
      RTO_timer_ = 0;
      RTO_ms_ = initial_RTO_ms_;
    }
  }

  // FIN
  // if ( reader().is_finished() ) {
  //   auto fin = make_empty_message();
  //   fin.FIN = true;
  //   transmit(fin);
  //   // Record
  //   outstanding_.push_back({
  //     abs_seqno_++,
  //     "",
  //   });
  //   ++sequence_number_in_flight_;
  //   if ( !RTO_timer_start_ ) {
  //     RTO_timer_start_ = true;
  //     RTO_timer_ = 0;
  //     RTO_ms_ = initial_RTO_ms_;
  //   }
  // }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage{
    Wrap32::wrap( abs_seqno_ , isn_ ),
    false,
    "",
    false,
    reader().has_error(),
  };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if (!msg.ackno.has_value()) {
    if ( msg.window_size == 0) {
      input_.set_error();
      return;
    }
    if ( abs_ackno_ == 0 ) {
      rwnd_ = msg.window_size;
    }
    return;
  }

  // Get abs ackno from ackno
  uint64_t ackno_ = msg.ackno.value().unwrap( isn_, abs_ackno_ );
  
  // Drop message.
  if ( ackno_ < abs_ackno_ || ackno_ > abs_seqno_ ) {
    return;
  }

  // Update ackno
  abs_ackno_ = ackno_;

  // Update receiver window size
  rwnd_ = msg.window_size;

  // Pop out outstanding segments
  while ( !outstanding_.empty() ) {
    auto it = outstanding_.begin();
    auto seq_len = max( (uint64_t)1, it->data_.size() + it->SYN_ + it->FIN_ );
    if ( abs_ackno_ >= it->first_index_ + seq_len ) {
      sequence_number_in_flight_ -= seq_len;
      outstanding_.erase(it);
      RTO_ms_ = initial_RTO_ms_;
      RTO_timer_ = 0;
      consecutive_retransmissions_ = 0;
    } else {
      break;
    }
  }

  if ( outstanding_.empty() ) {
    RTO_timer_start_ = false;
    RTO_timer_ = 0;
    RTO_ms_ = initial_RTO_ms_;
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  if ( RTO_timer_start_ ) {
    RTO_timer_ += ms_since_last_tick;
    if ( RTO_timer_ >= RTO_ms_ && !outstanding_.empty() ) {
      RTO_timer_ = 0;
      auto it = outstanding_.begin();
      auto segment = make_empty_message();
      segment.seqno = Wrap32::wrap( it->first_index_ , isn_ );
      segment.SYN = it->SYN_;
      segment.FIN = it->FIN_;
      segment.payload = move( it->data_ );
      transmit( segment );
      it->data_ = move( segment.payload );
      ++consecutive_retransmissions_;
      if ( rwnd_ > 0 ) {
        RTO_ms_ <<= 1;
      }
    }
  }
}
