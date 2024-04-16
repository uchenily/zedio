#pragma once

#include "zedio/core.hpp"
#include "zedio/log.hpp"
#include "zedio/net.hpp"

// C++
#include <array>
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

        return MessageType{
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
    // TODO: vector?
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
