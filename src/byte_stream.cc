#include "byte_stream.hh"
#include "debug.hh"

using namespace std;

ByteStream::ByteStream(uint64_t capacity) : capacity_(capacity), buffer_(std::vector<char>()) {}

// Push data to stream, but only as much as available capacity allows.
void Writer::push(string data)
{
    // If stream is closed, set error
    if (is_closed_ && !data.empty())
    {
        set_error();
        return;
    }

    // Trim data to available capacity
    uint64_t cur_capacity = available_capacity();
    if (data.size() > cur_capacity)
        data = data.substr(0, cur_capacity);

    // Append data to buffer
    for (char c : data)
        buffer_.push_back(c);
    bytes_pushed_ += data.size();

    debug("Writer::push({})", data);
}

// Signal that the stream has reached its ending. Nothing more will be written.
void Writer::close()
{
    is_closed_ = true;
    debug("Writer::close()");
}

// Has the stream been closed?
bool Writer::is_closed() const
{
    debug("Writer::is_closed() -> {}", is_closed_);
    return is_closed_;
}

// How many bytes can be pushed to the stream right now?
uint64_t Writer::available_capacity() const
{
    uint64_t available_capacity = capacity_ - (bytes_pushed_ - bytes_popped_);
    debug("Writer::available_capacity() -> {}", available_capacity);
    return available_capacity;
}

// Total number of bytes cumulatively pushed to the stream
uint64_t Writer::bytes_pushed() const
{
    debug("Writer::bytes_pushed() -> {}", bytes_pushed_);
    return bytes_pushed_;
}

// Peek at the next bytes in the buffer -- ideally as many as possible.
// It's not required to return a string_view of the *whole* buffer, but
// if the peeked string_view is only one byte at a time, it will probably force
// the caller to do a lot of extra work.
string_view Reader::peek() const
{
    debug("Reader::peek() called");
    if (buffer_.empty())
    {
        return {};
    }

    return string_view(buffer_.data(), buffer_.size());
}

// Remove `len` bytes from the buffer.
void Reader::pop(uint64_t len)
{
    if (is_finished() || len > bytes_buffered())
    {
        set_error();
        return;
    }

    bytes_popped_ += len;
    buffer_.erase(buffer_.begin(), buffer_.begin() + len);

    debug("Reader::pop({})", len);
}

// Is the stream finished (closed and fully popped)?
bool Reader::is_finished() const
{
    bool is_empty = bytes_buffered() == 0;
    bool is_finished = is_closed_;

    debug("Reader::is_finished() -> {}", is_empty && is_finished);

    return is_empty && is_finished;
}

// Number of bytes currently buffered (pushed and not popped)
uint64_t Reader::bytes_buffered() const
{
    debug("Reader::bytes_buffered() -> {}", bytes_pushed_ - bytes_popped_);
    return bytes_pushed_ - bytes_popped_;
}

// Total number of bytes cumulatively popped from stream
uint64_t Reader::bytes_popped() const
{
    debug("Reader::bytes_popped() -> {}", bytes_popped_);
    return bytes_popped_;
}
