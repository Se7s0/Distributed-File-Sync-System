#pragma once

#include <variant>
#include <string>
#include <optional>

namespace dfs {

template<typename T, typename E = std::string>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(E error) : data_(std::move(error)) {}

    bool is_ok() const { return std::holds_alternative<T>(data_); }
    bool is_error() const { return std::holds_alternative<E>(data_); }

    T& value() { return std::get<T>(data_); }
    const T& value() const { return std::get<T>(data_); }
    
    E& error() { return std::get<E>(data_); }
    const E& error() const { return std::get<E>(data_); }

    T value_or(T default_value) const {
        return is_ok() ? value() : std::move(default_value);
    }

private:
    std::variant<T, E> data_;
};

template<typename E>
class Result<void, E> {
public:
    Result() : error_(std::nullopt) {}
    Result(E error) : error_(std::move(error)) {}

    bool is_ok() const { return !error_.has_value(); }
    bool is_error() const { return error_.has_value(); }
    
    const E& error() const { return error_.value(); }

private:
    std::optional<E> error_;
};

template<typename T>
Result<T> Ok(T value) { return Result<T>(std::move(value)); }

template<typename E = std::string>
Result<void, E> Ok() { return Result<void, E>(); }

template<typename T, typename E>
Result<T, E> Err(E error) { return Result<T, E>(std::move(error)); }

} // namespace dfs