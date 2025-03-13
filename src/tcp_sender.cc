#include "tcp_sender.hh"
#include "tcp_config.hh"
#include <cstdint>
#include <ctime>
#include <iostream>
#include <sys/types.h>

using namespace std;

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return sequence_numbers_in_flight_;
}

// This function is for testing only; don't add extra state to support it.
uint64_t TCPSender::consecutive_retransmissions() const
{
  return consecutive_retransmissions_;
}

void TCPSender::push( const TransmitFunction& transmit )
{
  const uint64_t window = window_size_ == 0 ? 1 : window_size_;
  while ( window > sequence_numbers_in_flight_ ) {
    if ( FIN_sent_ ) {
      break;
    }

    auto msg = make_empty_message();
    if ( !SYN_sent_ ) {
      msg.SYN = true;
      SYN_sent_ = true;
    }

    const uint64_t avalible_space = window - sequence_numbers_in_flight_;
    const uint64_t payload_size = min( TCPConfig::MAX_PAYLOAD_SIZE, avalible_space - msg.sequence_length() );
    auto&& payload = msg.payload ;
    while ( reader().bytes_buffered() != 0 && payload.size() < payload_size ) {
      string_view view = reader().peek();
      auto len = payload_size - payload.size();
      view = view.substr( 0, len );
      payload += view;
      reader().pop( len );
    }

    if ( !FIN_sent_ && avalible_space > msg.sequence_length() && reader().is_finished() ) {
      msg.FIN = true;
      FIN_sent_ = true;
    }

    if ( msg.sequence_length() == 0 ) {
      break;
    }

    transmit( msg );
    if ( !timer_.is_active() ) {
      timer_.start();
    }
    stream_bytes_written_ += msg.sequence_length();
    sequence_numbers_in_flight_ += msg.sequence_length();
    outstanding_segments_.push_back( move(msg) );
  }
}

TCPSenderMessage TCPSender::make_empty_message() const
{
  TCPSenderMessage msg;
  msg.seqno = next_seqno();
  msg.RST = input_.has_error();
  return msg;
}

void TCPSender::receive( const TCPReceiverMessage& msg )
{
  if ( input_.has_error() ) {
    return;
  }

  if ( msg.RST ) {
    input_.set_error();
    return;
  }

  window_size_ = msg.window_size;

  if ( !msg.ackno.has_value() ) {
    return;
  }

  uint64_t abs_ackno = msg.ackno->unwrap( isn_, stream_bytes_written_ );

  if ( abs_ackno > stream_bytes_written_ ) {
    return;
  }

  bool has_acknoledgment = false;
  while ( !outstanding_segments_.empty() ) {
    const auto& seg = outstanding_segments_.front();
    uint64_t seg_end = seg.seqno.unwrap( isn_, stream_bytes_written_ ) + seg.sequence_length();

    if ( seg_end <= abs_ackno ) {
      has_acknoledgment = true;
      sequence_numbers_in_flight_ -= seg.sequence_length();
      outstanding_segments_.pop_front();
    } else {
      break;
    }
  }

  if ( has_acknoledgment ) {
    consecutive_retransmissions_ = 0;
    timer_.reload_RTO( initial_RTO_ms_ );
    if ( outstanding_segments_.empty() ) {
      timer_.stop();
    } else {
      timer_.start();
    }
  }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  timer_.tick( ms_since_last_tick );
  if ( timer_.is_expired() ) {
    if ( outstanding_segments_.empty() ) {
      return;
    }
    transmit( outstanding_segments_.front() );
    if ( window_size_ != 0 ) {
      consecutive_retransmissions_++;
      timer_.exponential_backoff();
    }
    timer_.reset();
  }
}

void RetransmissionTimer::start()
{
  is_active_ = true;
  reset();
}

void RetransmissionTimer::stop()
{
  is_active_ = false;
  reset();
}

void RetransmissionTimer::reset()
{
  time_ = 0;
}

void RetransmissionTimer::reload_RTO( uint64_t initial_RTO_ms )
{
  RTO_ = initial_RTO_ms;
}

void RetransmissionTimer::exponential_backoff()
{
  RTO_ *= 2;
}

bool RetransmissionTimer::is_expired()
{
  return time_ >= RTO_;
}

bool RetransmissionTimer::is_active()
{
  return is_active_;
}

void RetransmissionTimer::tick( uint64_t ms_since_last_tick )
{
  time_ += ms_since_last_tick;
}