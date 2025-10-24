#include "reassembler.hh"
#include "debug.hh"

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // debug( "unimplemented insert({}, {}, {}) called", first_index, data, is_last_substring );
  auto& writer = output_.writer();

  // Record last byte index
  if ( is_last_substring ) {
    has_last_substring_ = true;
    last_index_ = first_index + data.size();
  }

  // Truncate segment by available capacity
  uint64_t l1 = first_unassembled_index_;
  uint64_t r1 = first_unassembled_index_ + writer.available_capacity();
  uint64_t l2 = first_index;
  uint64_t r2 = first_index + data.size();
  uint64_t l = max( l1, l2 );
  uint64_t r = min( r1, r2 );
  if ( r > l ) {
    auto substring = data.substr( l - first_index, r - l );
    insert( l, substring );
  }

  // Write to byte stream
  if ( !lst_.empty() && lst_.begin()->first_index_ == first_unassembled_index_ ) {
    writer.push( lst_.begin()->data_ );
    first_unassembled_index_ = lst_.begin()->first_index_ + lst_.begin()->data_.size();
    rbtree_.erase( lst_.begin()->first_index_ );
    lst_.erase( lst_.begin() );
  }

  debug( "first_unassembled_index_ {} first_index {}", first_unassembled_index_, first_index );

  // When next byte == last byte then finish
  if ( first_unassembled_index_ == last_index_ && has_last_substring_ ) {
    writer.close();
  }
}

void Reassembler::insert( const uint64_t first_index, const string data )
{
  // Put first node into rbtree and list
  if ( rbtree_.empty() ) {
    lst_.push_back( { first_index, data } );
    rbtree_[first_index] = lst_.begin();
    return;
  }

  // Find the first node->first_index >= first_index
  auto it = rbtree_.lower_bound( first_index );
  if ( it != rbtree_.end() ) {
    // Need to merge current segment with the target node because they have same first_index, map can only save
    // unique key
    if ( it->first == first_index ) {
      if ( it->second->data_.size() < data.size() ) {
        it->second->data_ = data;
      }
    } else {
      rbtree_[first_index] = lst_.insert( it->second, { first_index, data } );
    }
  } else {
    lst_.push_back( { first_index, data } );
    rbtree_[first_index] = prev( lst_.end() );
  }

  auto start = rbtree_[first_index];

  if ( start != lst_.begin() && prev( start )->first_index_ + prev( start )->data_.size() >= first_index ) {
    start = prev( start );
  }

  merge( start );
}

void Reassembler::merge( list<Segment>::iterator node )
{
  if ( node == lst_.end() )
    return;

  auto next_node = next( node );
  while ( next_node != lst_.end() ) {
    uint64_t r1 = node->first_index_ + node->data_.size() - 1;
    uint64_t l2 = next_node->first_index_;

    if ( r1 + 1 >= l2 ) {
      if ( r1 < next_node->first_index_ + next_node->data_.size() - 1 ) {
        node->data_ += next_node->data_.substr( r1 - l2 + 1, next_node->data_.size() - ( r1 - l2 + 1 ) );
      }
      rbtree_.erase( next_node->first_index_ );
      lst_.erase( next_node );
      next_node = next( node );
    } else {
      break;
    }
  }
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  // debug( "unimplemented count_bytes_pending() called" );

  uint64_t cnt = 0;
  for ( auto it = lst_.begin(); it != lst_.end(); it = next( it ) ) {
    cnt += it->data_.size();
  }
  return cnt;
}
