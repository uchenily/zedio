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
#include <unordered_map>
#include <utility>

#include "examples/rpc_util.hpp"

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio::example;
using namespace zedio;

class RpcServer;
auto process(TcpStream stream, RpcServer *server) -> Task<void>;

class RpcServer {
public:
    RpcServer(std::string_view host, uint16_t port)
        : host_{host}
        , port_{port} {}

public:
    template <typename Func>
    void register_handler(std::string_view method, Func fn) {
        // 注册一个lambda表达式, 后续调用时通过传入的result获取调用的结果(字符串)
        map_invokers[{method.data(), method.size()}]
            = [fn]([[maybe_unused]] std::string_view serialized_parameters, std::string &result) {
                  // 从序列化后的数据中读取参数(类型和值)
                  // fn(arg1, arg2, ...);
                  // 处理参数会比较复杂, 先处理最简单的情况: 没有参数
                  auto res = fn();
                  // 获取函数的返回值, 并且序列化, 保存到result
                  // result = "42";
                  result = serialize<int>(res);
              };
    }

    auto run_handler(std::string_view method) -> std::string {
        console.debug("run_handler, method={}", method);
        std::string result;
        // map_invokers[method]("", result);
        auto it = map_invokers.find({method.data(), method.size()});
        if (it == map_invokers.end()) {
            result = "0005error";
        } else {
            it->second("", result);
        }
        return result;
    }

    void run() {
        auto runtime = Runtime::create();
        runtime.block_on([this]() -> Task<void> {
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
                    spawn(process(std::move(stream), this));
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
    std::unordered_map<std::string, std::function<void(std::string_view name, std::string &result)>>
        map_invokers{};
};

auto process(TcpStream stream, RpcServer *server) -> Task<void> {
    RpcFramed         rpc_framed{std::move(stream)};
    std::vector<char> buf(64);

    auto req = co_await rpc_framed.read_frame<RpcMessage>(buf);
    if (!req) {
        console.error("read rpc request failed: {}", req.error().message());
        co_return;
    }

    [[maybe_unused]] auto method_name = req.value().payload;
    // // TODO: run method by name
    // Person     p{"zhangsan", 18};
    // auto       data = p.serialize();
    // RpcMessage resp{data};

    auto       result = server->run_handler(method_name);
    RpcMessage resp{result};
    co_await rpc_framed.write_frame<RpcMessage>(resp);
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    RpcServer server{"127.0.0.1", 9000};
    // server.register_handler("add", [](int a, int b) -> int { return a + b; });
    // server.register_handler("get_person", []() -> Person {
    //     console.info("get_person called");
    //     return {"zhangsan", 18};
    // });
    server.register_handler("get_int", []() -> int {
        console.info("get_int called!");
        return 42;
    });
    server.run();
}
