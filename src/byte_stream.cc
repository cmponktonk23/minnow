#include "byte_stream.hh"
#include "debug.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) : capacity_( capacity )
{
  buffer_.reserve( capacity );
}

// Push data to stream, but only as much as available capacity allows.
void Writer::push( string data )
{
  // Your code here (and in each method below)
  // debug( "Writer::push({}) not yet implemented", data );

  const uint64_t curr_len = buffer_.size();
  const uint64_t add_len = data.size();
  if ( curr_len + add_len > capacity_ ) {
    buffer_ += data.substr( 0, capacity_ - curr_len );
  } else {
    buffer_ += data;
  }
  bytes_pushed_ += buffer_.size() - curr_len;
}

// Signal that the stream has reached its ending. Nothing more will be written.
void Writer::close()
{
  // debug( "Writer::close() not yet implemented" );
  is_closed_ = true;
}

// Has the stream been closed?
bool Writer::is_closed() const
{
  // debug( "Writer::is_closed() not yet implemented" );
  return is_closed_;
}

// How many bytes can be pushed to the stream right now?
uint64_t Writer::available_capacity() const
{
  // debug( "Writer::available_capacity() not yet implemented" );
  return capacity_ - buffer_.size();
}

// Total number of bytes cumulatively pushed to the stream
uint64_t Writer::bytes_pushed() const
{
  // debug( "Writer::bytes_pushed() not yet implemented" );
  return bytes_pushed_;
}

// Peek at the next bytes in the buffer -- ideally as many as possible.
// It's not required to return a string_view of the *whole* buffer, but
// if the peeked string_view is only one byte at a time, it will probably force
// the caller to do a lot of extra work.
string_view Reader::peek() const
{
  // debug( "Reader::peek() not yet implemented" );
  return buffer_;
}

// Remove `len` bytes from the buffer.
void Reader::pop( uint64_t len )
{
  // debug( "Reader::pop({}) not yet implemented", len );
  const uint64_t curr_len = buffer_.size();
  if ( len > curr_len ) {
    buffer_ = "";
  } else {
    buffer_ = buffer_.substr( len, curr_len - len );
  }
  bytes_popped_ += min( curr_len, len );
}

// Is the stream finished (closed and fully popped)?
bool Reader::is_finished() const
{
  // debug( "Reader::is_finished() not yet implemented" );
  return is_closed_ && buffer_.empty();
}

// Number of bytes currently buffered (pushed and not popped)
uint64_t Reader::bytes_buffered() const
{
  // debug( "Reader::bytes_buffered() not yet implemented" );
  return bytes_pushed_ - bytes_popped_;
}

// Total number of bytes cumulatively popped from stream
uint64_t Reader::bytes_popped() const
{
  // debug( "Reader::bytes_popped() not yet implemented" );
  return bytes_popped_;
}
