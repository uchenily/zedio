#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

// C
#include <cstdint>
// C++
#include <span>
#include <string>
#include <utility>
#include <vector>

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

struct HandshakeRequest {
    std::vector<char> methods;

    auto write_to(std::vector<char> &buf) {
        buf.push_back(SOCKS5_VERSION);
        buf.push_back(static_cast<char>(methods.size()));
        buf.insert(buf.end(), methods.begin(), methods.end());
    }
};

struct HandshakeResponse {
    uint8_t chosen_method;

    auto write_to(std::vector<char> &buf) const {
        buf.push_back(SOCKS5_VERSION);
        buf.push_back(static_cast<char>(chosen_method));
    }
};

class HandshakeCodec {
public:
    auto encode(const HandshakeResponse &message) -> std::vector<char> {
        std::vector<char> buf;
        message.write_to(buf);
        return buf;
    }
    auto decode([[maybe_unused]] std::span<char> buf) -> HandshakeRequest {
        return HandshakeRequest{};
    }
};

enum class Address : uint8_t {
    IPv4,
    IPv6,
    DomainName,
    Unknown,
};

struct CmdRequest {
    enum class Command : uint8_t {
        TCPConnect,
        TCPBind,
        UDPAssociate,
        OtherCommand,
    };
    /// SOCKS5 command
    Command command;
    /// Remote address
    /// TODO: 不只是Address类型, 也包含其他的信息, 比如 域名(string)
    Address address;
    /// Remot port
    uint16_t port;

    auto write_to(std::vector<char> &buf) const {
        buf.push_back(SOCKS5_VERSION);
        // buf.push_back(command.as_byte());
        buf.push_back(static_cast<char>(command));
        buf.push_back(SOCKS5_RESERVED);
        // address.write_to(buf);
        // buf.put_u16(self.port);
    }
};

struct CmdResponse {
    enum class Reply : uint8_t {
        Succeeded,
        GeneralFailure,
        ConnectionNotAllowed,
        NetworkUnreadchable,
        HostUnreachable,
        ConnectionRefused,
        TTLExpired,
        CommandNotSupported,
        AddressTypeNotSupported,
        OtherReply,
    };

    /// SOCKS5 reply
    Reply reply;
    /// Reply address
    Address  address;
    uint16_t port;

    auto write_to(std::vector<char> &buf) const {
        buf.push_back(SOCKS5_VERSION);
        // buf.push_back(reply.as_byte());
        buf.push_back(static_cast<char>(reply));
        buf.push_back(SOCKS5_RESERVED);
        // address.write_to(buf);
        // buf.put_u16(self.port);
    }
};

// TODO
struct BytesRequest {
    auto write_to([[maybe_unused]] std::vector<char> &buf) {}
};

// TODO
struct BytesResponse {
    // std::vector<char> inner;

    auto write_to([[maybe_unused]] std::vector<char> &buf) const {
        // buf.insert(buf.end(), inner.begin(), inner.end());
    }
};

class CmdCodec {
public:
    auto encode(CmdResponse &message) {
        std::vector<char> buf;
        message.write_to(buf);
        return buf;
    }
    auto decode([[maybe_unused]] std::span<char> buf) -> CmdRequest {
        return CmdRequest{};
    }
};

class BytesCodec {
public:
    auto encode(BytesResponse &message) {
        std::vector<char> buf;
        message.write_to(buf);
        return buf;
    }
    auto decode([[maybe_unused]] std::span<char> buf) -> BytesRequest {
        return BytesRequest{};
    }
};

template <typename Codec>
class Framed {
public:
    explicit Framed(TcpStream &&stream)
        : stream_{std::move(stream)} {}

    // Framed(Framed &&other) {
    //     codec_ = std::move(other.codec_);
    //     stream_ = std::move(other.stream_);
    // }
    // auto operator=(Framed &&other) -> Framed & {
    //     codec_ = std::move(other.codec_);
    //     stream_ = std::move(other.stream_);
    //     return *this;
    // }

    //~Framed() = default;

public:
    // 作用是读取一个完整的数据帧 (怎么表示? string? vector? buf? Result<>?)
    template <typename FrameType>
    auto read_frame(std::vector<char> &buf) -> Task<Result<FrameType>> {
        // 读取数据
        co_await stream_.read(buf);
        // 编码数据
        auto res = codec_.decode(buf);
        co_return res;
    }

    template <typename FrameType>
        requires requires(FrameType msg, std::vector<char> &buf) { msg.write_to(buf); }
    auto write_frame(FrameType &message) -> Task<void> {
        // 编码数据
        [[maybe_unused]] auto encoded = codec_.encode(message);
        // 写入数据

        // TODO: 添加一个模板方法: 一个类实现了 write_to(buf) 成员方法就可以调用
        // stream_.write(encoded);
        co_await stream_.write(encoded);
        co_return;
    }

    // auto take_stream() -> TcpStream && {
    auto take_stream() -> TcpStream {
        return std::move(stream_);
    }

private:
    Codec     codec_{};
    TcpStream stream_;
};

using HandshakeFramed = Framed<HandshakeCodec>;
using CmdFramed = Framed<CmdCodec>;
using BytesFramed = Framed<BytesCodec>;

auto socks5_handshake(TcpStream &stream) -> Task<Result<CmdFramed>> {
    HandshakeFramed   handshake_framed{std::move(stream)};
    std::vector<char> buf{};

    auto req = co_await handshake_framed.read_frame<HandshakeRequest>(buf);
    if (!req) {
        console.error("read handshake failed");
    }
    // console.debug("req: {}", req.value().methods[0]);

    HandshakeResponse resp{SOCKS5_AUTH_METHOD_NONE};
    co_await handshake_framed.write_frame(resp);

    // if (!req.methods.contains(&SOCKS5_AUTH_METHOD_NONE)) {
    //     co_return std::unexpected{make_zedio_error(Error::Unknown)};
    // }

    // CmdFramed cmd_framed{stream};
    // auto      cmd_req = co_await cmd_framed.read_frame(buf);
    // co_return cmd_framed;
    co_return CmdFramed{std::move(stream)};
}

auto socks5_connect(CmdFramed &cmd_framed, CmdRequest &req)
    -> Task<Result<std::pair<TcpStream, TcpStream>>> {
    // TODO
    std::string addr = "39.156.66.10";
    auto        sockaddr = SocketAddr{Ipv4Addr::parse(addr).value(), req.port};
    console.debug("try connect to {} ...", addr);
    auto outside = co_await TcpStream::connect(sockaddr);
    if (!outside) {
        console.error("connect {}:{} failed", addr, req.port);
        co_return std::unexpected{make_zedio_error(Error::Unknown)};
    }
    console.info("connect {}:{} successful", addr, req.port);

    co_return std::pair<TcpStream, TcpStream>(std::move(cmd_framed.take_stream()),
                                              std::move(outside.value()));
}

auto socks5_command(CmdFramed &cmd_framed) -> Task<Result<std::pair<BytesFramed, BytesFramed>>> {
    std::vector<char> buf{};
    auto              req = co_await cmd_framed.read_frame<CmdRequest>(buf);
    if (!req) {
        console.error("read command failed");
    }

    auto command_req = req.value();
    using Cmd = CmdRequest::Command;
    switch (command_req.command) {
    case Cmd::TCPConnect: {
        auto [stream, outside] = (co_await socks5_connect(cmd_framed, command_req)).value();
        // auto local = BytesFramed{std::move(stream)};
        BytesFramed local{std::move(stream)};
        // auto remote = BytesFramed{std::move(outside)};
        BytesFramed remote{std::move(outside)};

        // co_return std::make_pair(local, remote);
        co_return std::pair<BytesFramed, BytesFramed>(std::move(local), std::move(remote));
    }
    case Cmd::TCPBind:
    case Cmd::UDPAssociate:
    case Cmd::OtherCommand:
    default:
        console.error("unsupported command: {}", static_cast<uint8_t>(command_req.command));
        co_return std::unexpected{make_zedio_error(Error::Unknown)};
    }
}

auto socks5_streaming(BytesFramed &local, BytesFramed &remote) -> Task<void> {
    // TODO:
    // local --> remote
    // remote --> local
    std::vector<char> buf{};

    auto frame = co_await local.read_frame<BytesRequest>(buf);
    // BytesResponse resp{buf};
    BytesResponse resp;
    co_await remote.write_frame(resp);

    frame = co_await remote.read_frame<BytesRequest>(buf);
    // BytesResponse resp2{buf};
    BytesResponse resp2;
    co_await local.write_frame(resp2);
    co_return;
}

auto socks5_proxy(TcpStream stream) -> Task<void> {
    auto handshaked = co_await socks5_handshake(stream);
    if (handshaked) {
        // console.debug("handshake: {}", handshaked.value());
        console.debug("handshaked");
    }

    CmdFramed cmd_framed{std::move(handshaked.value())};
    [[maybe_unused]] auto [stream1, stream2] = (co_await socks5_command(cmd_framed)).value();
    co_await socks5_streaming(stream1, stream2);
}

auto server() -> Task<void> {
    auto addr = SocketAddr::parse("0.0.0.0", 1080);
    if (!addr) {
        console.error(addr.error().message());
        co_return;
    }

    auto has_listener = TcpListener::bind(addr.value());
    if (!has_listener) {
        console.error(has_listener.error().message());
        co_return;
    }

    console.info("Listening on 0.0.0.0:1080 ...");
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

// 现在看来实现代理服务器步骤还是挺多的, 不要一步跨太大了, 先从简单的开始做起吧
auto main() -> int {
    SET_LOG_LEVEL(zedio::log::LogLevel::Debug);
    auto runtime = Runtime::options().scheduler().set_num_workers(4).build();
    runtime.block_on(server());
    return 0;
}
