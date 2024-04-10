#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

using namespace zedio::async;
using namespace zedio::net;
using namespace zedio::log;
using namespace zedio;

static constexpr uint32_t SOCKS5_ERROR_UNSUPPORTED_VERSION = 501;
static constexpr uint32_t SOCKS5_ERROR_RESERVERD = 502;
static constexpr uint32_t SOCKS5_ERROR_UNKNOWN_ADDRTYPE = 503;

static constexpr uint8_t SOCKS5_VERSION = 0x05;
static constexpr uint8_t SOCKS5_RESERVED = 0x00;

static constexpr uint8_t SOCKS5_AUTH_METHOD_NONE = 0x00;
static constexpr uint8_t SOCKS5_AUTH_METHOD_GSSAPI = 0x01;
static constexpr uint8_t SOCKS5_AUTH_METHOD_PASSWORD = 0x02;
static constexpr uint8_t SOCKS5_AUTH_METHOD_NOTACCEPTABLE = 0xff;

static constexpr uint8_t SOCKS5_CMD_TCP_CONNECT = 0x01;
static constexpr uint8_t SOCKS5_CMD_TCP_BIND = 0x02;
static constexpr uint8_t SOCKS5_CMD_UDP_ASSOCIATE = 0x03;

static constexpr uint8_t SOCKS5_ADDR_IPV4 = 0x01;
static constexpr uint8_t SOCKS5_ADDR_DOMAINNAME = 0x03;
static constexpr uint8_t SOCKS5_ADDR_IPV6 = 0x04;

static constexpr uint8_t SOCKS5_REPLY_SUCCEEDED = 0x00;
static constexpr uint8_t SOCKS5_REPLY_GENERAL_FAILURE = 0x01;
static constexpr uint8_t SOCKS5_REPLY_CONNETCTION_NOT_ALLOWED = 0x02;
static constexpr uint8_t SOCKS5_REPLY_NETWORK_UNREACHABLE = 0x03;
static constexpr uint8_t SOCKS5_REPLY_HOST_UNREACHABLE = 0x04;
static constexpr uint8_t SOCKS5_REPLY_CONNECTION_REFUSED = 0x05;
static constexpr uint8_t SOCKS5_REPLY_TTL_EXPIRED = 0x06;
static constexpr uint8_t SOCKS5_REPLY_COMMAND_NOT_SUPPORTED = 0x07;
static constexpr uint8_t SOCKS5_REPLY_ADDRESS_TYPE_NOT_SUPPORTED = 0x08;

using BytesFramed = Framed<TcpStream, BytesCodec>;
using CmdFramed = Framed<TcpStream, CmdCodec>;

class HandshakeCodec {
public:
    auto encode() {}
    auto decode() {}
};

auto socks5_handshake(TcpStream stream) -> Task<Result<CmdFramed>> {
    Framed framed = {stream, HandshakeCodec};
    auto   req = co_await framed.next();
    if (!req) {
        console.error("read handshake failed");
    }

    HandshakeResponse resp{SOCKS5_AUTH_METHOD_NONE};
    co_await stream.write(resp);

    if (!req.methods.contains(&SOCKS5_AUTH_METHOD_NONE)) {
        co_return make_zedio_error{};
    }

    // framed.map_codec
}

auto socks5_proxy(TcpStream stream) -> Task<void> {
    auto handshaked = co_await socks5_handshake(stream);
    // auto [stream1, stream2] = co_await socks5_command(handshaked);
    // co_await socks5_streaming(s1, s2);
}

auto server() -> Task<void> {
    auto addr = SocketAddr::parse("127.0.0.1", 9898);
    if (!addr) {
        console.error(addr.error().message());
        co_return;
    }

    auto has_listener = TcpListener::bind(addr.value());
    if (!has_listener) {
        console.error(has_listener.error().message());
        co_return;
    }
    auto listener = std::move(has_listener.value());
    while (true) {
        // auto has_stream = co_await listener.accept().set_timeout(3s).set_exclusion();
        auto has_stream = co_await listener.accept();

        if (has_stream) {
            auto &[stream, peer_addr] = has_stream.value();
            console.info("Accept a connection from {}", peer_addr);
            spawn(socks5_proxy(std::move(stream)));
        } else {
            console.error(has_stream.error().message());
            break;
        }
    }
}

auto main(int argc, char **argv) -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::options().scheduler().set_num_workers(4).build();
    runtime.block_on(server());
    return 0;
}