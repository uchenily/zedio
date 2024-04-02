#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"
#include "zedio/time.hpp"

using namespace zedio;
using namespace zedio::log;
using namespace zedio::async;
using namespace zedio::net;

template <class Stream = TcpStream, class Addr = SocketAddr>
class Channel {
public:
    explicit Channel(Stream &&stream)
        : stream_{std::move(stream)} {
        std::tie(reader_, writer_) = stream_.into_split();
    }

public:
    auto Send(std::span<const char> message) -> Task<void> {
        char     msg_len[4];
        uint32_t length = message.size();

        msg_len[3] = length & 0xFF;
        msg_len[2] = (length >> 8) & 0xFF;
        msg_len[1] = (length >> 16) & 0xFF;
        msg_len[0] = (length >> 24) & 0xFF;

        auto ret = co_await writer_.write_all(msg_len);
        if (!ret) {
            console.error("send error: {}", ret.error().message());
            co_await Close();
            co_return;
        }

        ret = co_await writer_.write_all(message);
        if (!ret) {
            console.error("send error: {}", ret.error().message());
            co_await Close();
            co_return;
        }
        console.info("send succ");
    }

    auto Recv() -> Task<std::string> {
        char msg_len[4];
        auto ret = co_await reader_.read_exact(msg_len);
        if (!ret) {
            console.error("recv error: {}", ret.error().message());
            co_await Close();
            co_return std::string{};
        }

        auto length = msg_len[0] << 24 | msg_len[1] << 16 | msg_len[2] << 8 | msg_len[3];

        std::string message(length, 0);
        ret = co_await reader_.read_exact(message);
        if (!ret) {
            console.error("recv error: {}", ret.error().message());
            co_await Close();
            co_return std::string{};
        }
        console.info("recv succ");
        co_return message;
    }

    auto Close() -> Task<void> {
        co_await reader_.reunite(writer_).value().close();
    }

private:
    Stream                                                       stream_;
    zedio::socket::detail::BaseStream<Stream, Addr>::OwnedReader reader_{nullptr};
    zedio::socket::detail::BaseStream<Stream, Addr>::OwnedWriter writer_{nullptr};
};

auto process(TcpStream &&stream) -> Task<void> {
    Channel channel{std::move(stream)};

    co_await channel.Send("client test message1");
    auto message1 = co_await channel.Recv();
    console.info("message1: {}", message1);

    co_await channel.Send("client test message2");
    auto message2 = co_await channel.Recv();
    console.info("message2: {}", message2);

    co_await channel.Send("client test message3");
    auto message3 = co_await channel.Recv();
    console.info("message3: {}", message3);

    co_await channel.Close();
}

auto client() -> Task<void> {
    auto addr = SocketAddr::parse("localhost", 9999).value();
    auto ret = co_await TcpStream::connect(addr);
    if (!ret) {
        console.error("{}", ret.error().message());
        co_return;
    }
    auto stream = std::move(ret.value());
    co_await process(std::move(stream));
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Info);
    auto runtime = Runtime::options().scheduler().set_num_workers(1).build();
    // auto runtime = Runtime::create();
    runtime.block_on(client());
}
