#ifndef RESULT_HPP
#define RESULT_HPP

#include <cassert>
#include <string>
#include <variant>

template <typename T>
class Result {
public:
    Result(const T& value) : data{ value } {}
    Result(T&& value) : data{ std::move(value) } {}
    Result(const std::string& error) : data{ error } {}
    Result(std::string&& error) : data{ std::move(error) } {}
    Result(const char* error) : data{ std::string{error} } {}

    bool isError() const {
        return std::holds_alternative<std::string>(data);
    }

    const std::string& getError() const {
        assert(isError());
        return std::get<std::string>(data);
    }

    const T& getValue() const {
        assert(!isError());
        return std::get<T>(data);
    }

    T& getValue() {
        assert(!isError());
        return std::get<T>(data);
    }

private:
    std::variant<T, std::string> data;
};

#endif // RESULT_HPP