#include "zedio/core.hpp"
#include "zedio/io/buf/reader.hpp"
#include "zedio/io/buf/writer.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"
#include "zedio/socket/split.hpp"
#include "zedio/socket/stream.hpp"

namespace zedio::utils {

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio;

using OwnedReader = socket::detail::OwnedReadHalf<TcpStream, SocketAddr>;
using OwnedWriter = socket::detail::OwnedWriteHalf<TcpStream, SocketAddr>;
using BufferedReader = BufReader<OwnedReader>;
using BufferedWriter = BufWriter<OwnedWriter>;

template <typename Derived>
class Codec {
public:
    auto Decode(BufferedReader &reader) -> Task<std::string> {
        co_return co_await static_cast<Derived *>(this)->decode(reader);
    }

    auto Encode(const std::span<const char> message, BufferedWriter &writer) -> Task<void> {
        co_await static_cast<Derived *>(this)->encode(message, writer);
    }
};

class LengthLimitedCodec : public Codec<LengthLimitedCodec> {
private:
    friend class Codec<LengthLimitedCodec>;

    auto decode(BufferedReader &reader) -> Task<std::string> {
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

    auto encode(const std::span<const char> message, BufferedWriter &writer) -> Task<void> {
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
    explicit Channel(TcpStream &&stream) {
        auto [reader, writer] = stream.into_split();
        buffered_reader_ = BufReader(std::move(reader));
        buffered_writer_ = BufWriter(std::move(writer));
    }

public:
    [[REMEMBER_CO_AWAIT]]
    auto Send(std::span<const char> message) -> Task<void> {
        co_await codec_.Encode(message, buffered_writer_);
    }

    [[REMEMBER_CO_AWAIT]]
    auto Recv() -> Task<std::string> {
        co_return co_await codec_.Decode(buffered_reader_);
    }

    [[REMEMBER_CO_AWAIT]]
    auto Close() -> Task<void> {
        co_await buffered_reader_.inner().reunite(buffered_writer_.inner()).value().close();
    }

private:
    BufferedReader     buffered_reader_{OwnedReader{nullptr}};
    BufferedWriter     buffered_writer_{OwnedWriter{nullptr}};
    LengthLimitedCodec codec_;
};

} // namespace zedio::utils
