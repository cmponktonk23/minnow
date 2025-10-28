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
  // Bug: 这里当FIN = true时就不要再继续循环了，否则会不停的发送FIN包！
  while ( !is_finished_ ) {
    string_view stream = reader().peek();

    uint64_t limit = 0;
    
    // 这里是无符号整数，减法不会得到负数，所以要先判大小再做减法
    // 这里 rwnd 可以为 0，但是实际运算时要当作 1 来处理
    if ( sequence_number_in_flight_ < max( rwnd_, (uint16_t)1 ) ) {
      /** 
       * Bug: SIN FIN要占据序列号空间，但是它不占据payload空间。而rwnd约束的是序列号空间，MAX_PAYLOAD_SIZE(MSS)约束的是payload长度
       * 理论上这俩不应该放到一起决定序列号空间长度上限，但是可以通过将MAX_PAYLOAD_SIZE+2来将它变为考虑了SYN+Payload+Fin
       * 但是这里还要考虑下面取stream的substr的时候不能取到MAX_PAYLOAD_SIZE+2这么长，因为payload最长只能是MAX_PAYLOAD_SIZE
       */
        limit = min( max( rwnd_, (uint16_t)1 ) - sequence_number_in_flight_, TCPConfig::MAX_PAYLOAD_SIZE + 2 );
    }

    if ( limit == 0 ) return;

    auto segment = make_empty_message();

    /**
     * 两种情况：
     * 1. 当前序列号空间=1，只能单发一个SYN
     * 2. 当前序列号空间>1，发送SYN+Payload
     */
    if ( first_msg_ ) {
      first_msg_ = false;
      segment.SYN = true;
      --limit;
    }
    
    // 因为上面得到的limit是序列号空间的上限，可能会超过MAX_PAYLOAD_SIZE，所以当用limit决定payload长度时要和MAX_PAYLOAD_SIZE取最小值
    string payload { stream.substr( 0, min( TCPConfig::MAX_PAYLOAD_SIZE, min( limit, stream.size() ) ) ) };
    reader().pop( payload.size() );
    limit -= payload.size();

    /**
     * 两种情况：
     * 1. 如果当前序列号空间够，再带上FIN
     * 2. 如果当前序列号空间不够，单独发个FIN
     */
     if ( reader().is_finished() && limit > 0 ) {
      segment.FIN = true;
      is_finished_ = true;
    }

    segment.payload = move( payload );

    /**
     * Bug: 这里要先记录下来sequence_length，因为下面为了避免string copy，将segment.payload直接move给了自定义的segment
     * 导致后面调用sequence_length时payload长度已经为0了
     */
    auto seq_len = segment.sequence_length();
    
    if ( seq_len == 0 ) return;

    transmit( segment );

    // Bug: 这里Segment里面必须要记录SYN和FIN，相当于TCPSendMessage里有的字段都要记录
    outstanding_.push_back({
      abs_seqno_,
      segment.SYN,
      segment.FIN,
      move( segment.payload ),
    });

    // Advance absolute seqno
    abs_seqno_ += seq_len;
    sequence_number_in_flight_ += seq_len;

    // Restart timer everytime send a msg
    if ( !RTO_timer_start_ ) {
      RTO_timer_start_ = true;
      RTO_timer_ = 0;
      RTO_ms_ = initial_RTO_ms_;
    }
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  return TCPSenderMessage{
    Wrap32::wrap( abs_seqno_ , isn_ ),
    false,
    "",
    false,
    reader().has_error(),  // Bug: msg.RST直接和byte-stream的error绑定
  };
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if (!msg.ackno.has_value()) {
    if ( msg.window_size == 0) {
      input_.set_error();  // Bug: ack<none> + wind_size=0 set byte stream error
      return;
    }
    if ( abs_ackno_ == 0 ) {
      rwnd_ = msg.window_size; // Bug: ack<none>的时候不能直接视为invalid，仍旧需要更新window size
    }
    return;
  }

  // Get abs ackno from ackno
  uint64_t ackno_ = msg.ackno.value().unwrap( isn_, abs_ackno_ );
  
  // ackno比当前absolute ackno小时直接drop
  // Bug: ackno比当前absolute seqno还大时直接不合法
  if ( ackno_ < abs_ackno_ || ackno_ > abs_seqno_ ) {
    return;
  }

  // Update ackno and rwnd
  abs_ackno_ = ackno_;
  rwnd_ = msg.window_size;

  // Pop out outstanding segments
  while ( !outstanding_.empty() ) {
    auto it = outstanding_.begin();
    auto seq_len = max( (uint64_t)1, it->sequence_length() );
    if ( abs_ackno_ >= it->first_index + seq_len ) {
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
      segment.seqno = Wrap32::wrap( it->first_index , isn_ );
      segment.SYN = it->SYN;
      segment.FIN = it->FIN;
      segment.payload = move( it->payload );
      transmit( segment );

      it->payload = move( segment.payload );
      ++consecutive_retransmissions_;
      
      // Bug: 只有在rwnd nonzero的时候才能倍增RTO（看文档）
      if ( rwnd_ > 0 ) {
        RTO_ms_ <<= 1;
      }
    }
  }
}
