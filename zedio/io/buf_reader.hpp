#pragma once

#include "zedio/io/buf/reader.hpp"

namespace zedio::io {

template <class IO>
    requires requires(IO io, std::span<char> buf) {
        { io.read(buf) };
    }
class BufReader : public detail::Reader<BufReader<IO>> {
private:
    friend class detail::Reader<BufReader<IO>>;

public:
    BufReader(IO &&io, std::size_t size = detail::StreamBuffer::DEFAULT_BUF_SIZE)
        : io_{std::move(io)}
        , r_stream_{size} {}

    BufReader(BufReader &&other)
        : io_{std::move(other.io_)}
        , r_stream_{std::move(other.r_stream_)} {}

    auto operator=(BufReader &&other) -> BufReader & {
        io_ = std::move(other.io_);
        r_stream_ = std::move(other.r_stream_);
        return *this;
    }

private:
    IO                   io_;
    detail::StreamBuffer r_stream_;
};

} // namespace zedio::io
