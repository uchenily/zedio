#include "zedio/core.hpp"
#include "zedio/io/buf/reader.hpp"
#include "zedio/io/buf/writer.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"
#include "zedio/socket/split.hpp"
#include "zedio/socket/stream.hpp"

// C
#include <cassert>
#include <cstdint>
// C++
#include <format>
#include <string>
#include <string_view>
#include <utility>

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio;

#include "rpc_util.hpp"

using RpcFramed = Framed<RpcCodec<RpcResponse, RpcRequest>>;

auto process(TcpStream stream) -> Task<void> {
    RpcFramed         rpc_framed{std::move(stream)};
    std::vector<char> buf(64);

    auto req = co_await rpc_framed.read_frame<RpcRequest>(buf);
    if (!req) {
        console.error("read rpc request failed: {}", req.error().message());
        co_return;
    }

    [[maybe_unused]] auto method_name = req.value().method;
    // TODO: run method by name
    Person      p{"zhangsan", 18};
    auto        data = p.serialize();
    RpcResponse resp{data};
    co_await rpc_framed.write_frame<RpcResponse>(resp);
}

auto server() -> Task<void> {
    std::string host = "127.0.0.1";
    uint16_t    port = 9000;

    auto has_addr = SocketAddr::parse("127.0.0.1", 9000);
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
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::create();
    runtime.block_on(server());
}
