#pragma once

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

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
    static auto deserialize(const std::string &data) -> Person {
        std::istringstream iss(data);
        std::string        name;
        int                age{};
        iss >> name >> age;
        return {name, age};
    }
};

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
