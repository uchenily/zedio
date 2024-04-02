#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

using namespace zedio::async;
using namespace zedio::net;
using namespace zedio::log;
using namespace zedio;

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

auto process(TcpStream stream) -> Task<void> {
    Channel channel{std::move(stream)};

    auto message1 = co_await channel.Recv();
    console.info("message1: {}", message1);
    co_await channel.Send("server test message1");

    auto message2 = co_await channel.Recv();
    console.info("message2: {}", message2);
    co_await channel.Send("server test message2");

    auto message3 = co_await channel.Recv();
    console.info("message3: {}", message3);
    co_await channel.Send("server test message3");

    co_await channel.Close();
}

auto server() -> Task<void> {
    auto has_addr = SocketAddr::parse("127.0.0.1", 9999);
    if (!has_addr) {
        console.error(has_addr.error().message());
        co_return;
    }
    auto has_listener = TcpListener::bind(has_addr.value());
    if (!has_listener) {
        console.error(has_listener.error().message());
        co_return;
    }
    auto listener = std::move(has_listener.value());
    while (true) {
        auto has_stream = co_await listener.accept();

        if (has_stream) {
            auto &[stream, peer_addr] = has_stream.value();
            console.info("Accept a connection from {}", peer_addr);
            spawn(process(std::move(stream)));
        } else {
            console.error(has_stream.error().message());
            break;
        }
    }
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Info);
    auto runtime = Runtime::options().scheduler().set_num_workers(1).build();
    // auto runtime = Runtime::create();
    runtime.block_on(server());
}
