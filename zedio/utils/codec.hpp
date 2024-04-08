#include "zedio/core.hpp"
#include "zedio/io/buf/stream.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

namespace zedio::utils {

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio;

template <typename Derived, typename BufferedStream>
class Codec {
public:
    auto Decode(BufferedStream &reader) -> Task<std::string> {
        co_return co_await static_cast<Derived *>(this)->decode(reader);
    }

    auto Encode(const std::span<const char> message, BufferedStream &writer) -> Task<void> {
        co_await static_cast<Derived *>(this)->encode(message, writer);
    }
};

template <typename BufferedStream>
class LengthLimitedCodec : public Codec<LengthLimitedCodec<BufferedStream>, BufferedStream> {
private:
    friend Codec<LengthLimitedCodec<BufferedStream>, BufferedStream>;

    auto decode(BufferedStream &reader) -> Task<std::string> {
        std::array<unsigned char, 4> msg_len{};
        auto                         ret = co_await reader.read_exact(
            {reinterpret_cast<char *>(msg_len.data()), msg_len.size()});
        if (!ret) {
            console.error("decode error: {}", ret.error().message());
            co_return std::string{};
        }

        auto length = msg_len[0] << 24 | msg_len[1] << 16 | msg_len[2] << 8 | msg_len[3];

        std::string message(length, 0);
        ret = co_await reader.read_exact(message);
        if (!ret) {
            console.error("decode error: {}", ret.error().message());
            co_return std::string{};
        }
        co_return message;
    }

    auto encode(const std::span<const char> message, BufferedStream &writer) -> Task<void> {
        std::array<unsigned char, 4> msg_len{};
        uint32_t                     length = message.size();

        msg_len[3] = length & 0xFF;
        msg_len[2] = (length >> 8) & 0xFF;
        msg_len[1] = (length >> 16) & 0xFF;
        msg_len[0] = (length >> 24) & 0xFF;

        auto ret
            = co_await writer.write_all({reinterpret_cast<char *>(msg_len.data()), msg_len.size()});
        if (!ret) {
            console.error("encode error: {}", ret.error().message());
            co_return;
        }

        ret = co_await writer.write_all(message);
        if (!ret) {
            console.error("encode error: {}", ret.error().message());
            co_return;
        }

        co_await writer.flush();
    }
};

class Channel {
public:
    explicit Channel(TcpStream &&stream)
        : buffered_stream_{std::move(stream)} {}

public:
    auto Send(std::span<const char> message) -> Task<void> {
        co_await codec_.Encode(message, buffered_stream_);
    }

    auto Recv() -> Task<std::string> {
        auto message = co_await codec_.Decode(buffered_stream_);
        co_return message;
    }

    auto Close() -> Task<void> {
        co_await buffered_stream_.into_inner().close();
    }

private:
    BufStream<TcpStream>                     buffered_stream_;
    LengthLimitedCodec<BufStream<TcpStream>> codec_;
};

} // namespace zedio::utils
