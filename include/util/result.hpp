#ifndef RESULT_HPP
#define RESULT_HPP

#include <cassert>
#include <string>
#include <utility>
#include <variant>

template <typename T>
class Result {
public:
    Result(const T& value) : m_data{ value } {}
    Result(T&& value) : m_data{ std::move(value) } {}
    Result(const std::string& error) : m_data{ error } {}
    Result(std::string&& error) : m_data{ std::move(error) } {}
    Result(const char* error) : m_data{ std::string{error} } {}

    bool isValue() const {
        return std::holds_alternative<T>(m_data);
    }

    bool isError() const {
        return std::holds_alternative<std::string>(m_data);
    }

    const T& getValue() const {
        assert(isValue());
        return std::get<T>(m_data);
    }

    T& getValue() {
        assert(isValue());
        return std::get<T>(m_data);
    }

    const std::string& getError() const {
        assert(isError());
        return std::get<std::string>(m_data);
    }

private:
    std::variant<T, std::string> m_data;
};

#endif // RESULT_HPP