#include "reassembler.hh"
#include "debug.hh"

using namespace std;

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  // debug( "unimplemented insert({}, {}, {}) called", first_index, data, is_last_substring );
  auto &writer = output_.writer();

  // Record last byte
  if (is_last_substring) {
    has_last_substring_ = true;
    last_byte_ = first_index + data.size();
  }

  uint64_t l1 = next_byte_;
  uint64_t r1 = next_byte_ + writer.available_capacity();
  uint64_t l2 = first_index;
  uint64_t r2 = first_index + data.size();

  uint64_t l = max(l1, l2);
  uint64_t r = min(r1, r2);
  if (r > l) {
    store(data, l - next_byte_, l - first_index, r - first_index);
  }

  // Move forward
  forward();

  // When next byte == last byte then finish
  if (has_last_substring_ && next_byte_ == last_byte_) {
    writer.close();
  }
}

void Reassembler::store(const string &data, uint64_t offset, uint64_t start, uint64_t end)
{
  if (start >= end) return;

  auto substring = data.substr(start, end - start);
  uint64_t pos = (curr_ + offset) % capacity_;
  for (char c : substring) {
    buffer_[pos] = c;
    uint64_t x = pos / 64;
    uint64_t y = pos % 64;
    bitmap_[x] |= ((uint64_t)1 << y);
    pos = (pos + 1) % capacity_;
  }
}

void Reassembler::forward()
{
  uint64_t old_pos = curr_;
  while(1) {
    uint64_t x = curr_ / 64;
    uint64_t y = curr_ % 64;
    uint64_t bits = bitmap_[x];
    uint64_t rev = ~(bits >> y);
    uint64_t cnt;
    if (rev == 0) {
      cnt = 64;
    } else {
      cnt = __builtin_ctzll(rev);
    }
    next_byte_ += cnt;
    curr_ = (curr_ + cnt) % capacity_;
    if (cnt < 64) {
      bitmap_[x] ^= ((((uint64_t)1 << cnt) - 1) << y);
    } else {
      bitmap_[x] = 0;
    }
    if ( cnt != 64 - (curr_ == 0 ? 64 - capacity_ % 64 : 0) - y ) break;
  }
  write(old_pos, curr_);
}

void Reassembler::write(uint64_t start, uint64_t end) {
  if (start == end) return;
  auto &writer = output_.writer();
  string data;
  if (start < end) {
    data = string(buffer_.begin() + start, buffer_.begin() + end);
  } else {
    data = string(buffer_.begin() + start, buffer_.end()) + string(buffer_.begin(), buffer_.begin() + end);
  }
  writer.push( data );
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  // debug( "unimplemented count_bytes_pending() called" );
  uint64_t ones = 0;
  for ( auto bits : bitmap_ ) {
    while ( bits > 0 ) {
      bits &= (bits - 1);
      ++ones;
    }
  }
  return ones;
}
