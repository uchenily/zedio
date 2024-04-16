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

using RpcFramed = Framed<RpcCodec<RpcRequest, RpcResponse>>;

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
        requires requires(std::string_view buf) { T::deserialize(buf); }
    auto call([[maybe_unused]] std::string_view method_name) -> Task<Result<T>> {
        RpcFramed         rpc_framed{std::move(stream_)};
        std::vector<char> buf(64);

        RpcRequest req{.method = method_name};
        co_await rpc_framed.write_frame<RpcRequest>(req);

        auto resp = co_await rpc_framed.read_frame<RpcResponse>(buf);
        if (!resp) {
            console.error("receive rpc response failed");
            co_return std::unexpected{make_zedio_error(Error::Unknown)};
        }

        console.info("data from rpc server: {}", resp.value().payload);
        auto data = resp.value().payload;
        co_return T::deserialize(data);
        // co_return T{"zhangsan", 18};
    }

private:
    TcpStream stream_;
};

auto client() -> Task<void> {
    auto res = co_await RpcClient::connect("127.0.0.1", 9000);
    if (!res) {
        console.error("connect failed");
        co_return;
    }
    auto client = std::move(res.value());
    auto person = (co_await client.call<Person>("get_person")).value();
    console.info("get_person name={}, age={}", person.name, person.age);
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::options().scheduler().set_num_workers(1).build();
    runtime.block_on(client());
}
