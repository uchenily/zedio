#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"
#include "zedio/time.hpp"
#include "zedio/utils/codec.hpp"

using namespace zedio;
using namespace zedio::log;
using namespace zedio::async;
using namespace zedio::net;
using namespace zedio::utils;

auto process(TcpStream &&stream) -> Task<void> {
    Channel channel{std::move(stream)};

    for (auto i = 0u; i < 64; i++) {
        co_await channel.Send(std::format("client message round {}", i));
        auto message = co_await channel.Recv();
        console.info("Received: {}", message);
    }
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
