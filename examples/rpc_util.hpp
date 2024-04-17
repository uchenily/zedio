#pragma once

#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

// C++
#include <array>
#include <concepts>
#include <expected>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

// C
#include <cstdint>

// Linux
#include <arpa/inet.h>
#include <netinet/in.h>
#include <type_traits>

namespace zedio::example {

using namespace zedio::async;
using namespace zedio::net;
using namespace zedio::log;

static constexpr size_t FIXED_LEN{4uz};

auto read_u32(std::span<char> bytes) -> uint32_t {
    uint32_t value = 0;
    value |= static_cast<uint32_t>(bytes[0]) << 24;
    value |= static_cast<uint32_t>(bytes[1]) << 16;
    value |= static_cast<uint32_t>(bytes[2]) << 8;
    value |= static_cast<uint32_t>(bytes[3]);
    return value;

    // // use ntohl()
    // // value |= static_cast<uint32_t>(bytes[3]) << 24;
    // // value |= static_cast<uint32_t>(bytes[2]) << 16;
    // // value |= static_cast<uint32_t>(bytes[1]) << 8;
    // // value |= static_cast<uint32_t>(bytes[0]);
    // // memcpy(&value, bytes.data(), sizeof(uint32_t));
    // std::copy(bytes.data(),
    //           bytes.data() + sizeof(uint32_t),
    //           reinterpret_cast<unsigned char *>(&value));
    // return ntohl(value);
}

void write_u32(uint32_t value, std::span<uint8_t> bytes) {
    bytes[0] = static_cast<uint8_t>((value >> 24) & 0xFF);
    bytes[1] = static_cast<uint8_t>((value >> 16) & 0xFF);
    bytes[2] = static_cast<uint8_t>((value >> 8) & 0xFF);
    bytes[3] = static_cast<uint8_t>(value & 0xFF);

    // // use htonl()
    // value = htonl(value); // 转换为网络字节序
    // // memcpy(bytes.data(), &value, sizeof(uint32_t));  // 将值拷贝到字节数组中
    // std::copy(reinterpret_cast<const unsigned char *>(&value),
    //           reinterpret_cast<const unsigned char *>(&value) + sizeof(uint32_t),
    //           bytes.data());
}

// template <typename T>
// concept Deserializable = requires(std::string_view buf) {
//     { T::deserialize(buf) } -> std::convertible_to<T>;
// };

template <typename T>
// or requires Deserializable<T>
    requires requires(std::string_view buf) {
        { T::deserialize(buf) } -> std::convertible_to<T>;
    }
static auto deserialize(std::string_view data) -> T {
    console.debug("data: `{}`, data.size: {}", data, data.size());
    return T::deserialize({data.data(), data.size()});
}

template <typename T>
    requires std::is_fundamental_v<T>
// requires requires { std::is_fundamental_v<T>; } // 这种为什么不对?
static auto deserialize(std::string_view data) -> T {
    console.debug("data: `{}`, data.size: {}", data, data.size());
    std::istringstream iss({data.begin(), data.size()});

    T t;
    iss >> t;
    console.debug("deserialize: {}", t);
    return t;
}

template <typename T>
    requires requires(T t) {
        { t.serialize() } -> std::convertible_to<std::string_view>;
        // t.serialize();
    }
static auto serialize(T t) -> std::string {
    return t.serialize();
}

// template <typename T>
//     requires requires { std::is_fundamental_v<T>; }
template <typename T>
    requires std::is_fundamental_v<T>
static auto serialize(T t) -> std::string {
    std::ostringstream oss;
    oss << t;
    return oss.str();
}

struct RpcMessage {
    std::string_view payload;

    void write_to(std::string &buf) const {
        std::array<unsigned char, 4> bytes{};
        uint32_t                     length = payload.size();

        write_u32(length, bytes);
        buf.append(std::string_view{reinterpret_cast<char *>(bytes.data()), bytes.size()});
        buf.append(payload);
    }
};

template <typename MessageType>
class RpcCodec {
public:
    auto encode(MessageType &message) -> std::string {
        std::string buf;
        message.write_to(buf);
        return buf;
    }
    auto decode(std::span<char> buf) -> Result<MessageType> {
        // FIXME: 用buf.size判断有问题
        if (buf.size() < FIXED_LEN) {
            console.error("buf.size less then fixed length");
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }
        auto length = read_u32(buf);
        console.debug("decode buf:`{}`, bytes_to_len: {}",
                      std::string_view{buf.data(), buf.size()},
                      length);

        if (buf.size() - FIXED_LEN < length) {
            console.error("buf.size less then fixed length + message length");
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }

        console.debug("!!!payload: {}",
                      std::string_view{buf.begin() + FIXED_LEN, buf.begin() + FIXED_LEN + length});
        return MessageType{
            std::string_view{buf.begin() + FIXED_LEN, buf.begin() + FIXED_LEN + length}
        };
    }
};

template <typename Codec>
class Framed {
public:
    explicit Framed(TcpStream &stream)
        : stream_{stream} {}

public:
    // 读取一个完整的数据帧
    template <typename FrameType>
    auto read_frame(std::span<char> buf) -> Task<Result<FrameType>> {
        // 读取数据
        std::size_t nbytes = 0;
        while (true) {
            auto s = buf.subspan(nbytes, buf.size() - nbytes);
            auto read_result = co_await stream_.read(buf);
            if (!read_result) {
                console.error("read_frame :{}", read_result.error().message());
                co_return std::unexpected{make_sys_error(errno)};
            }
            if (read_result.value() == 0) {
                break;
            }
            nbytes += read_result.value();
            // 解码数据
            auto readed = buf.subspan(0, nbytes);
            auto res = codec_.decode(readed);
            if (!res) {
                // FIXME
                continue;
            }
            co_return res;
        }

        // co_return FrameType{""};
        co_return std::unexpected{make_zedio_error(Error::UnexpectedEOF)};
    }

    // 写入一个完整的数据帧
    template <typename FrameType>
        requires requires(FrameType msg, std::string &buf) { msg.write_to(buf); }
    // auto write_frame(FrameType &message) -> Task<Result<void>> { 不支持Result<void>, assert会报错
    auto write_frame(FrameType &message) -> Task<Result<bool>> {
        // 编码数据
        auto encoded = codec_.encode(message);
        // 写入数据

        // TODO: 添加一个模板方法: 一个类实现了 write_to(buf) 成员方法就可以调用
        // stream_.write(encoded);
        console.debug("write_frame encoded: `{}`, length: {}", encoded, encoded.size());
        auto write_result = co_await stream_.write(encoded);
        if (!write_result) {
            console.error("write_frame error: {}", write_result.error().message());
            co_return std::unexpected{
                make_zedio_error(Error::Unknown)}; // make_sys_error(errno) -> Success?
        }
        co_return true;
    }

private:
    Codec      codec_{};
    TcpStream &stream_;
};

class Person {
public:
    std::string name;
    int         age;

    // 构造函数
    Person(std::string_view name, int age)
        : name{name}
        , age{age} {}

    // 序列化函数
    [[nodiscard]]
    auto serialize() const -> std::string {
        std::ostringstream oss;
        oss << name << " " << age;
        return oss.str();
    }

    // 反序列化函数
    [[nodiscard]]
    static auto deserialize(std::string_view data) -> Person {
        std::istringstream iss({data.begin(), data.size()});
        std::string        name;
        int                age{};
        iss >> name >> age;
        return {name, age};
    }
};

using RpcFramed = Framed<RpcCodec<RpcMessage>>;

// auto main() -> int {
//     // 创建一个 Person 对象
//     Person person("zhangsan", 18);
//
//     // 序列化对象
//     std::string serialized_data = person.serialize();
//     std::cout << "Serialized data: `" << serialized_data << "`\n";
//
//     // 反序列化对象
//     Person new_person = Person::deserialize(serialized_data);
//     std::cout << "New Person name: " << new_person.name << ", age: " << new_person.age << '\n';
//
//     return 0;
// }

} // namespace zedio::example
