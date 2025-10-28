#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return Wrap32 { zero_point.raw_value_ + static_cast<uint32_t>( n ) };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint64_t diff1, diff2, diff3;
  uint64_t mod = ( 1UL << 32 );

  uint64_t x = 0;
  if ( zero_point.raw_value_ <= raw_value_ ) {
    x = raw_value_ - zero_point.raw_value_;
    diff1 = checkpoint > x ? checkpoint - x : x - checkpoint;
  } else {
    diff1 = mod << 1;
  }

  uint64_t ret = mod - zero_point.raw_value_ + raw_value_;
  uint64_t k = checkpoint >= ret ? ( checkpoint - ret ) / mod : 0;
  uint64_t y1 = k * mod + ret;
  uint64_t y2 = ( k + 1 ) * mod + ret;

  diff2 = checkpoint > y1 ? checkpoint - y1 : y1 - checkpoint;

  if ( y2 > y1 ) {
    diff3 = checkpoint > y2 ? checkpoint - y2 : y2 - checkpoint;
  } else {
    diff3 = mod << 1;
  }

  auto min_diff = min( min( diff1, diff2 ), diff3 );
  if ( min_diff == diff1 )
    return x;
  if ( min_diff == diff2 )
    return y1;
  return y2;
}
