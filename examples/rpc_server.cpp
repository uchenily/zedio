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

#include "examples/rpc_util.hpp"

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio::example;
using namespace zedio;

auto process(TcpStream stream) -> Task<void> {
    RpcFramed         rpc_framed{std::move(stream)};
    std::vector<char> buf(64);

    auto req = co_await rpc_framed.read_frame<RpcMessage>(buf);
    if (!req) {
        console.error("read rpc request failed: {}", req.error().message());
        co_return;
    }

    [[maybe_unused]] auto method_name = req.value().payload;
    // TODO: run method by name
    Person     p{"zhangsan", 18};
    auto       data = p.serialize();
    RpcMessage resp{data};
    co_await rpc_framed.write_frame<RpcMessage>(resp);
}

class RpcServer {
public:
    RpcServer(std::string_view host, uint16_t port)
        : host_{host}
        , port_{port} {}

public:
    void register_handler() {}

    void run() {
        auto runtime = Runtime::create();
        runtime.block_on([]() -> Task<void> {
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
        }());
    }

private:
    std::string host_;
    uint16_t    port_;
};

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    RpcServer server{"127.0.0.1", 9000};
    server.run();
}
