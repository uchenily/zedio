#pragma once

#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

// C++
#include <array>
#include <concepts>
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

auto bytes_to_len(std::span<char> bytes) -> uint32_t {
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

void len_to_bytes(uint32_t value, std::span<uint8_t> bytes) {
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

template <typename T>
// requires requires(std::string_view buf) { T::deserialize(buf)->T; }
// requires requires(std::string_view buf) { T::deserialize(buf); }
    requires requires(std::string_view buf) {
        { T::deserialize(buf) } -> std::convertible_to<T>;
    }
static auto deserialize(std::string_view data) -> T {
    console.debug("data: `{}`, data.size: {}", data, data.size());
    return T::deserialize({data.data(), data.size()});
}

// 好像必须使用enable_if, 只使用requires可能是因为函数签名一样的原因会报错:
// error: call of overloaded ‘deserialize<...>’ is ambiguous
template <typename T>
static auto deserialize(std::string_view data) ->
    typename std::enable_if<std::is_fundamental<T>::value, T>::type {
    // requires requires { std::is_fundamental_v<T> && !std::is_class_v<T>; }
    // static auto deserialize([[maybe_unused]] T &t0, std::string_view data) -> T {
    console.debug("data: `{}`, data.size: {}", data, data.size());
    // std::istringstream iss({data.begin() + FIXED_LEN, data.size() - FIXED_LEN});
    std::istringstream iss({data.begin(), data.size()});

    T t;
    iss >> t;
    console.debug("deserialize: {}", t);
    return t;
}

template <typename T>
    requires requires(T t) {
        // { t.serialize() } -> std::convertible_to<std::string_view>;
        t.serialize();
    }
static auto serialize(T t) -> std::string {
    return t.serialize();
}

// template <typename T>
//     requires requires { std::is_fundamental_v<T>; }
template <typename T>
static auto serialize(T t) ->
    typename std::enable_if<std::is_fundamental<T>::value, std::string>::type {
    std::ostringstream           oss;
    std::array<unsigned char, 4> bytes{};

    oss << t;
    auto temp = oss.str();
    len_to_bytes(temp.size(), bytes);
    auto res = std::string{reinterpret_cast<char *>(bytes.data()), bytes.size()};
    res.append(temp);
    return res;
}

struct RpcMessage {
    std::string_view payload;

    void write_to(std::string &buf) const {
        std::array<unsigned char, 4> bytes{};
        uint32_t                     length = payload.size();

        len_to_bytes(length, bytes);
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
        if (buf.size() < FIXED_LEN) {
            console.error("buf.size less then fixed length");
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }
        auto length = bytes_to_len(buf);

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
    explicit Framed(TcpStream &&stream)
        : stream_{std::move(stream)} {}

public:
    // 读取一个完整的数据帧
    template <typename FrameType>
    auto read_frame(std::span<char> buf) -> Task<Result<FrameType>> {
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

private:
    Codec     codec_{};
    TcpStream stream_;
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
