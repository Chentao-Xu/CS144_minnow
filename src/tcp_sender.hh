#pragma once

#include "byte_stream.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"

#include <cstdint>
#include <deque>
#include <functional>

class RetransmissionTimer
{
public:
  RetransmissionTimer( uint64_t initial_RTO_ms ) : RTO_( initial_RTO_ms ) {}
  void start();
  void stop();
  void reset();
  void reload_RTO( uint64_t initial_RTO_ms );
  void exponential_backoff();
  bool is_expired();
  bool is_active();
  void tick( uint64_t ms_since_last_tick );
private:
  uint64_t RTO_;
  bool is_active_ {false};
  uint64_t time_ {0};
};

class TCPSender
{
  public:
  /* Construct TCP sender with given default Retransmission Timeout and possible ISN */
    TCPSender( ByteStream&& input, Wrap32 isn, uint64_t initial_RTO_ms )
      : input_( std::move( input ) )
      , isn_( isn )
      , initial_RTO_ms_( initial_RTO_ms )
      , timer_( initial_RTO_ms )
    {}

  /* Generate an empty TCPSenderMessage */
  TCPSenderMessage make_empty_message() const;

  /* Receive and process a TCPReceiverMessage from the peer's receiver */
  void receive( const TCPReceiverMessage& msg );

  /* Type of the `transmit` function that the push and tick methods can use to send messages */
  using TransmitFunction = std::function<void( const TCPSenderMessage& )>;

  /* Push bytes from the outbound stream */
  void push( const TransmitFunction& transmit );

  /* Time has passed by the given # of milliseconds since the last time the tick() method was called */
  void tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit );

  // Accessors
  uint64_t sequence_numbers_in_flight() const;  // For testing: how many sequence numbers are outstanding?
  uint64_t consecutive_retransmissions() const; // For testing: how many consecutive retransmissions have happened?
  const Writer& writer() const { return input_.writer(); }
  const Reader& reader() const { return input_.reader(); }
  Writer& writer() { return input_.writer(); }

private:
  Reader& reader() { return input_.reader(); }

  ByteStream input_;
  Wrap32 isn_;
  uint64_t initial_RTO_ms_;
  uint64_t window_size_ { 1 };

  bool SYN_sent_ { false };
  bool FIN_sent_ { false };

  uint64_t stream_bytes_written_ { 0 };
  uint64_t consecutive_retransmissions_ { 0 };
  uint64_t sequence_numbers_in_flight_ { 0 };
  RetransmissionTimer timer_;

  std::deque<TCPSenderMessage> outstanding_segments_ {};

  Wrap32 next_seqno() const { return isn_ + stream_bytes_written_; }
};
