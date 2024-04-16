#pragma once

#include "zedio/core.hpp"
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

namespace zedio::example {

using namespace zedio::async;
using namespace zedio::net;

struct RpcMessage {
    std::string_view payload;

    void write_to(std::string &buf) const {
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

template <typename MessageType>
class RpcCodec {
public:
    auto encode(MessageType &message) -> std::string {
        std::string buf;
        message.write_to(buf);
        return buf;
    }
    auto decode(std::span<char> buf) -> Result<MessageType> {
        if (buf.size() < 4uz) {
            return std::unexpected{make_zedio_error(Error::Unknown)};
        }
        auto length = buf[0] << 24 | buf[1] << 16 | buf[2] << 8 | buf[3];

        if (buf.size() < 4uz + length) {
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
        : name(name)
        , age(age) {}

    // 序列化函数
    [[nodiscard]]
    auto serialize() const -> std::string {
        std::ostringstream oss;
        oss << name << " " << age;
        return oss.str();
    }

    // 反序列化函数
    static auto deserialize(std::string_view data) -> Person {
        std::istringstream iss({data.begin(), data.size()});
        std::string        name;
        int                age{};
        iss >> name >> age;
        return {name, age};
    }
};

using RpcFramed = Framed<RpcCodec<RpcMessage>>;

#if 0
auto main() -> int {
    // 创建一个 Person 对象
    Person person("zhangsan", 18);

    // 序列化对象
    std::string serialized_data = person.serialize();
    std::cout << "Serialized data: `" << serialized_data << "`\n";

    // 反序列化对象
    Person new_person = Person::deserialize(serialized_data);
    std::cout << "New Person name: " << new_person.name << ", age: " << new_person.age << '\n';

    return 0;
}
#endif

} // namespace zedio::example
