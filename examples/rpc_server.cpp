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

    auto write_to(std::string &buf) {
        std::array<unsigned char, 4> msg_len{};
        uint32_t                     length = payload.size();

        msg_len[3] = length & 0xFF;
        msg_len[2] = (length >> 8) & 0xFF;
        msg_len[1] = (length >> 16) & 0xFF;
        msg_len[0] = (length >> 24) & 0xFF;
        buf.append(std::string_view{reinterpret_cast<char *>(msg_len.data()), msg_len.size()});
        buf.append(payload);
    }
};

class RpcCodec {
public:
    auto encode(RpcResponse &message) -> std::string {
        std::string buf;
        message.write_to(buf);
        return buf;
    }
    auto decode([[maybe_unused]] std::span<char> buf) -> Result<RpcRequest> {
        if (buf.size() < 4uz) {
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }
        auto length = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

        if (buf.size() < 4uz + length) {
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }

        return RpcRequest{
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
