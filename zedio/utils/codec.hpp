#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

namespace zedio::utils {

using namespace zedio::log;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio::socket::detail;
using namespace zedio;

template <typename Derived, typename Stream, typename Addr>
class Codec {
public:
    auto Decode(BaseStream<Stream, Addr>::OwnedReader &reader) -> Task<std::string> {
        co_return co_await static_cast<Derived *>(this)->decode(reader);
    }

    auto Encode(const std::span<const char> message, BaseStream<Stream, Addr>::OwnedWriter &writer)
        -> Task<void> {
        co_await static_cast<Derived *>(this)->encode(message, writer);
    }
};

template <typename Stream, typename Addr>
class LengthLimitedCodec : public Codec<LengthLimitedCodec<Stream, Addr>, Stream, Addr> {
private:
    friend Codec<LengthLimitedCodec<Stream, Addr>, Stream, Addr>;
    auto decode(BaseStream<Stream, Addr>::OwnedReader &reader) -> Task<std::string> {
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

    auto encode(const std::span<const char> message, BaseStream<Stream, Addr>::OwnedWriter &writer)
        -> Task<void> {
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
    }
};

template <class Stream = TcpStream,
          class Addr = SocketAddr,
          class MsgCodec = LengthLimitedCodec<Stream, Addr>>
class Channel {
public:
    explicit Channel(Stream &&stream)
        : stream_{std::move(stream)} {
        std::tie(reader_, writer_) = stream_.into_split();
    }

public:
    auto Send(std::span<const char> message) -> Task<void> {
        co_await codec_.Encode(message, writer_);
    }

    auto Recv() -> Task<std::string> {
        auto message = co_await codec_.Decode(reader_);
        co_return message;
    }

    auto Close() -> Task<void> {
        co_await reader_.reunite(writer_).value().close();
    }

private:
    Stream                                stream_;
    BaseStream<Stream, Addr>::OwnedReader reader_{nullptr};
    BaseStream<Stream, Addr>::OwnedWriter writer_{nullptr};
    Codec<MsgCodec, Stream, Addr>         codec_;
};

} // namespace zedio::utils
