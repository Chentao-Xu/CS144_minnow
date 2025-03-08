#include "reassembler.hh"
#include <ranges>
#include <iostream>

using namespace std;

auto Reassembler::split( uint64_t pos ) noexcept
{
  auto it = buffer_.lower_bound( pos );

  if ( it != buffer_.end() && it->first == pos ) {
    return it;
  }
  if ( it == buffer_.begin() ) {
    return it; // buffer_为空或pos在最小索引之前
  }

  const auto pre_it = prev( it );
  if ( pre_it->first + pre_it->second.size() > pos ) {
    const auto insert_it = buffer_.emplace_hint( it, pos, pre_it->second.substr( pos - pre_it->first ) );
    pre_it->second.resize( pos - pre_it->first );
    return insert_it;
  }
  return it;
}

void Reassembler::try_close() noexcept
{
  if ( next_index_.has_value() && next_index_.value() == writer().bytes_pushed() ) {
    output_.writer().close();
  }
}

void Reassembler::insert( uint64_t first_index, string data, bool is_last_substring )
{
  if ( data.empty() ) {
    if ( !next_index_.has_value() && is_last_substring ) {
      next_index_.emplace( first_index );
    }
    try_close();
    return;
  }

  if ( writer().is_closed() || writer().available_capacity() == 0 ) {
    return;
  }

  const uint64_t first_unassembled_index = writer().bytes_pushed();
  const uint64_t first_unacceptable_index = first_unassembled_index + writer().available_capacity();

  if ( first_index + data.size() <= first_unassembled_index || first_index >= first_unacceptable_index ) {
    return; // 数据超出缓冲区
  }

  if ( first_index + data.size() > first_unacceptable_index ) {
    data.resize( first_unacceptable_index - first_index );
    is_last_substring = false;
  }

  if ( first_index < first_unassembled_index ) {
    data.erase( 0, first_unassembled_index - first_index );
    first_index = first_unassembled_index;
  }

  if ( !next_index_.has_value() && is_last_substring ) {
    next_index_.emplace( first_index + data.size() );
  }

  auto upper = split( first_index + data.size() );
  auto lower = split( first_index );

  for ( const auto& str : ranges::subrange( lower, upper ) | views::values ) {
    pending_bytes_ -= str.size();
  }

  pending_bytes_ += data.size();
  buffer_.emplace_hint( buffer_.erase( lower, upper ), first_index, move( data ) );

  while ( !buffer_.empty() ) {
    auto&& [index, payload] = *buffer_.begin();
    if ( index != writer().bytes_pushed() ) {
      break;
    }

    pending_bytes_ -= payload.size();
    output_.writer().push( move( payload ) );
    buffer_.erase( buffer_.begin() );
  }
  
  try_close();
  return;
}

// How many bytes are stored in the Reassembler itself?
// This function is for testing only; don't add extra state to support it.
uint64_t Reassembler::count_bytes_pending() const
{
  return pending_bytes_;
}
