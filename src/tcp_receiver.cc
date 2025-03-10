#include "tcp_receiver.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  if ( writer().has_error() ) {
    return;
  }

  if ( message.RST ) {
    reader().set_error();
    return;
  }

  if ( message.SYN && !isn_.has_value() ) {
    isn_ = message.seqno;
  }

  if ( !isn_.has_value() ) {
    return;
  }

  uint64_t abs_seqno = message.seqno.unwrap( isn_.value(), writer().bytes_pushed() );
  uint64_t stream_index = abs_seqno - 1;
  if ( message.SYN ) {
    stream_index = 0; // SYN占一位
  }
  reassembler_.insert( stream_index, move( message.payload ), message.FIN );
}

TCPReceiverMessage TCPReceiver::send() const
{
  TCPReceiverMessage msg;

  if ( isn_.has_value() ) {
    uint64_t next_seq = reassembler_.writer().bytes_pushed() + 1;
    if ( reassembler_.writer().is_closed() ) { // FIN占一位
      next_seq += 1;
    }
    msg.ackno = Wrap32::wrap( next_seq, isn_.value() );
  }
  msg.window_size
    = static_cast<uint16_t>( min<uint64_t>( UINT16_MAX, reassembler_.writer().available_capacity() ) );
  msg.RST = writer().has_error();

  return msg;
}
