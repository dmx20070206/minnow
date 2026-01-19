#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

// abs_seq -> seq
Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  Wrap32 result = zero_point + static_cast<uint32_t>( n );
  return result;
}

// seq -> abs_seq
uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  uint32_t offset = raw_value_ - zero_point.raw_value_;

  uint64_t t = ( checkpoint & 0xFFFFFFFF00000000 ) + offset;
  uint64_t res = t;

  if ( res < checkpoint && ( checkpoint - res ) > ( 1ULL << 31 ) )
    res += ( 1ULL << 32 );
  else if ( res > checkpoint && ( res - checkpoint ) > ( 1ULL << 31 ) ) {
    if ( res >= ( 1ULL << 32 ) )
      res -= ( 1ULL << 32 );
  }

  return res;
}
