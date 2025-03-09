#include "wrapping_integers.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  return zero_point + static_cast<uint32_t>( n );
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  static constexpr uint64_t delta = 1ULL << 32 ;

  const uint64_t abs_base_seqno = raw_value_ - zero_point.raw_value_ ;
  const uint64_t checkpoint_low =  checkpoint & 0xFFFFFFFFULL;
  const uint64_t candidate = ( checkpoint & ~0xFFFFFFFFULL ) | abs_base_seqno;

  if ( candidate >= delta && abs_base_seqno > checkpoint_low && ( abs_base_seqno - checkpoint_low ) > ( delta / 2 ) ) {
    return candidate - delta;
  }
  if ( candidate < ~0xFFFFFFFFULL && checkpoint_low > abs_base_seqno && ( checkpoint_low - abs_base_seqno ) > ( delta / 2 ) ) {
    return candidate + delta;
  }
  return candidate;
}
