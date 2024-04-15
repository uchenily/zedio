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

struct RpcRequest {
    std::string_view method;

    auto write_to(std::string &buf) const {
        std::array<unsigned char, 4> msg_len{};
        uint32_t                     length = method.size();

        msg_len[3] = length & 0xFF;
        msg_len[2] = (length >> 8) & 0xFF;
        msg_len[1] = (length >> 16) & 0xFF;
        msg_len[0] = (length >> 24) & 0xFF;
        buf.append(std::string_view{reinterpret_cast<char *>(msg_len.data()), msg_len.size()});
        buf.append(method);
    }
};

struct RpcResponse {
    std::string_view payload;
};

class RpcCodec {
public:
    auto encode(const RpcRequest &message) -> std::string {
        std::string buf;
        message.write_to(buf);
        return buf;
    }
    auto decode([[maybe_unused]] std::span<char> buf) -> Result<RpcResponse> {
        if (buf.size() < 4uz) {
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }
        auto length = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

        if (buf.size() < 4uz + length) {
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }

        return RpcResponse{
            std::string_view{buf.begin() + 4uz, buf.begin() + 4uz + length}
        };
    }
};

template <typename Codec>
class Framed {
public:
    explicit Framed(TcpStream &&stream)
        : stream_{std::move(stream)} {}

public:
    // 读取一个完整的数据帧
    template <typename FrameType>
    auto read_frame(std::vector<char> &buf) -> Task<Result<FrameType>> {
        // 读取数据
        co_await stream_.read(buf);
        // 解码数据
        auto res = codec_.decode(buf);
        co_return res;
    }

    // 写入一个完整的数据帧
    template <typename FrameType>
        requires requires(FrameType msg, std::string &buf) { msg.write_to(buf); }
    auto write_frame(FrameType &message) -> Task<void> {
        // 编码数据
        auto encoded = codec_.encode(message);
        // 写入数据

        // TODO: 添加一个模板方法: 一个类实现了 write_to(buf) 成员方法就可以调用
        // stream_.write(encoded);
        co_await stream_.write(encoded);
        co_return;
    }

    // auto take_stream() -> TcpStream {
    //     return std::move(stream_);
    // }

private:
    Codec     codec_{};
    TcpStream stream_;
};

using RpcFramed = Framed<RpcCodec>;

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
