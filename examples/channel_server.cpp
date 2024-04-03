#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"
#include "zedio/utils/codec.hpp"

using namespace zedio::async;
using namespace zedio::net;
using namespace zedio::log;
using namespace zedio::utils;
using namespace zedio;

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
