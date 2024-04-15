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

#include "rpc_util.hpp"

using namespace zedio::log;
using namespace zedio::io;
using namespace zedio::net;
using namespace zedio::async;
using namespace zedio;

class RpcClient {
private:
    RpcClient(std::string_view host, uint16_t port)
        : host_{host}
        , port_{port} {}

public:
    static auto connect(std::string_view host, uint16_t port) -> Task<Result<RpcClient>> {
        co_return RpcClient{host, port};
    }

    template <typename T>
    auto call([[maybe_unused]] std::string_view method_name) -> Task<Result<T>> {
        co_return T{"zhangsan", 18};
    }

private:
private:
    std::string host_;
    uint16_t    port_;
};

auto client() -> Task<void> {
    auto res = co_await RpcClient::connect("127.0.0.1", 9000);
    if (!res) {
        console.error("connect failed");
        co_return;
    }
    auto client = res.value();
    co_await client.call<Person>("get_person");
}

auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::options().scheduler().set_num_workers(1).build();
    runtime.block_on(client());
}
