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

    for (auto i = 0u; i < 64; i++) {
        co_await channel.Send(std::format("server message round {}", i));
        auto message = co_await channel.Recv();
        console.info("Received: {}", message);
    }
    co_await channel.Close();
}

auto server() -> Task<void> {
    std::string host = "127.0.0.1";
    uint16_t    port = 9999;
    auto        has_addr = SocketAddr::parse(host, port);
    if (!has_addr) {
        console.error(has_addr.error().message());
        co_return;
    }
    auto has_listener = TcpListener::bind(has_addr.value());
    if (!has_listener) {
        console.error(has_listener.error().message());
        co_return;
    }
    console.info("Listening on {}:{} ...", host, port);
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
