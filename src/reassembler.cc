#include "reassembler.hh"
#include "debug.hh"

/**
 * 使用红黑树+双链表实现O(logn)的查找新substring的插入位置，并和左右邻居合并，保证每次维护完
 * 链表上每个节点和邻居都不能再合并。每次只需要将链表头节点write到bytestream即可O(1)。
 */

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  auto &writer = output_.writer();

  // Record last byte index
  if ( is_last_substring ) {
    has_last_substring_ = true;
    last_index_ = first_index + data.size();
  }

  // 这里用的都是stream index。用接收缓冲区剩余窗口区间和substring的stream index区间求交，得到等push到reassembler的bytes  
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
  if ( !lst_.empty() && lst_.begin()->first_index == first_unassembled_index_ ) {
    writer.push( lst_.begin()->payload );
    first_unassembled_index_ = lst_.begin()->first_index + lst_.begin()->payload.size();
    rbtree_.erase( lst_.begin()->first_index );
    lst_.erase( lst_.begin() );
  }

  // When next byte == last byte then finish
  if ( first_unassembled_index_ == last_index_ && has_last_substring_ ) {
    writer.close();
  }
}

void Reassembler::insert( const uint64_t first_index, const string data )
{
  // Put first node into rbtree and list
  if ( rbtree_.empty() ) {
    lst_.push_back( { first_index, false, false, data } );
    rbtree_[first_index] = lst_.begin();
    return;
  }

  // Find the first node->first_index >= first_index
  auto it = rbtree_.lower_bound( first_index );
  if ( it != rbtree_.end() ) {
    // Need to merge current segment with the target node because they have same first_index, map key must be unique
    if ( it->first == first_index ) {
      if ( it->second->payload.size() < data.size() ) {
        it->second->payload = data;
      }
    } else {
      rbtree_[first_index] = lst_.insert( it->second, { first_index, false, false, data } );
    }
  } else {
    rbtree_[first_index] = lst_.insert( lst_.end(), { first_index, false, false, data } );
  }

  // 新加的节点如果不是头节点，就往前走一个节点（因为这个节点可能可以和新插入的节点合并），然后从这个节点开始往后开始做区间合并，直到不能再合并则停止
  auto start = rbtree_[first_index];
  if ( start != lst_.begin() && prev( start )->first_index + prev( start )->payload.size() >= first_index ) {
    start = prev( start );
  }

  merge( start );
}

/**
 * 向右合并，只需要比较当前节点的右端点和下一个节点的左端点即可
 */
void Reassembler::merge( list<Segment>::iterator node )
{
  if ( node == lst_.end() )
    return;

  auto next_node = next( node );
  while ( next_node != lst_.end() ) {
    uint64_t r1 = node->first_index + node->payload.size() - 1;
    uint64_t l2 = next_node->first_index;

    if ( r1 + 1 >= l2 ) {
      if ( r1 < next_node->first_index + next_node->payload.size() - 1 ) {
        auto len = r1 - l2 + 1;
        node->payload += next_node->payload.substr( len , next_node->payload.size() - len );
      }
      rbtree_.erase( next_node->first_index );
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
  uint64_t cnt = 0;
  for ( auto it = lst_.begin(); it != lst_.end(); it = next( it ) ) {
    cnt += it->payload.size();
  }
  return cnt;
}
