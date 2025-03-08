#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity ), buffer_() {}

void Writer::push( string data )
{
  // 容量检查
  uint64_t space_left = this->available_capacity();
  if ( data.size() > space_left ) {
    data = data.substr( 0, space_left );
  }

  // 将数据添加到缓冲区
  buffer_.append( data );
  bytes_pushed_ += data.size();
}

void Writer::close()
{
  closed_ = true;
}

bool Writer::is_closed() const
{
  return closed_;
}

uint64_t Writer::available_capacity() const
{
  return capacity_ - buffer_.size();
}

uint64_t Writer::bytes_pushed() const
{
  return bytes_pushed_;
}

string_view Reader::peek() const
{
  return string_view( buffer_ );
}

void Reader::pop( uint64_t len )
{
  if ( len > buffer_.size() ) {
    len = buffer_.size();
  }
  buffer_.erase( 0, len );
  bytes_popped_ += len;
}

bool Reader::is_finished() const
{
  return closed_ && buffer_.empty();
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size();
}

uint64_t Reader::bytes_popped() const
{
  return bytes_popped_;
}
