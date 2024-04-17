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

class RpcClient {
private:
    explicit RpcClient(TcpStream &&stream)
        : stream_{std::move(stream)} {}

public:
    static auto connect(std::string_view host, uint16_t port) -> Task<Result<RpcClient>> {
        auto addr = SocketAddr::parse(host, port).value();
        auto stream = co_await TcpStream::connect(addr);
        if (!stream) {
            console.error("TcpStream::connect failed: {}", stream.error().message());
            co_return std::unexpected{make_sys_error(errno)};
        }
        co_return RpcClient{std::move(stream.value())};
    }

    template <typename T>
    auto call(std::string_view method_name) -> Task<Result<T>> {
        // 之前将stream_通过std::move移动到RpcFramed中, 这样直到在一次call后,
        // 同一个client第二次执行call时, TcpStream已经被析构了, 所以服务端co_await stream_.read(xx)
        // 返回值大小为0(读取的字节数) 现在改成传入左值引用修复这个问题
        RpcFramed              rpc_framed{stream_};
        std::array<char, 1024> buf{};

        RpcMessage req{method_name};
        co_await rpc_framed.write_frame<RpcMessage>(req);

        auto resp = co_await rpc_framed.read_frame<RpcMessage>(buf);
        if (!resp) {
            console.error("receive rpc response failed: {}", resp.error().message());
            co_return std::unexpected{make_zedio_error(Error::Unknown)};
        }

        console.info("data from rpc server: {}", resp.value().payload);
        auto data = resp.value().payload;
        co_return deserialize<T>(data);
    }

private:
    TcpStream stream_;
};

auto client() -> Task<void> {
    auto res = co_await RpcClient::connect("127.0.0.1", 9000);
    if (!res) {
        console.error("connect failed: {}", res.error().message());
        co_return;
    }
    auto client = std::move(res.value());

    auto person = (co_await client.call<Person>("get_person")).value();
    console.info("get_person name={}, age={}", person.name, person.age);

    auto int_result = (co_await client.call<int>("get_int")).value();
    console.info("get_int {}", int_result);
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::options().scheduler().set_num_workers(1).build();
    runtime.block_on(client());
}
