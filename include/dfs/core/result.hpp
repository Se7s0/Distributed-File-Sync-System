#pragma once

#include <variant>
#include <string>
#include <optional>

namespace dfs {

// Helper wrapper types for disambiguation when T == E
template<typename T>
struct OkValue {
    T value;
    explicit OkValue(T v) : value(std::move(v)) {}
};

template<typename E>
struct ErrValue {
    E error;
    explicit ErrValue(E e) : error(std::move(e)) {}
};

template<typename T, typename E = std::string>
class Result {
private:
    std::variant<T, E> data_;

public:
    // Constructor for success value (using wrapper)
    Result(OkValue<T> ok) : data_(std::in_place_index<0>, std::move(ok.value)) {}

    // Constructor for error value (using wrapper)
    Result(ErrValue<E> err) : data_(std::in_place_index<1>, std::move(err.error)) {}

    bool is_ok() const { return data_.index() == 0; }
    bool is_error() const { return data_.index() == 1; }

    T& value() { return std::get<0>(data_); }
    const T& value() const { return std::get<0>(data_); }

    E& error() { return std::get<1>(data_); }
    const E& error() const { return std::get<1>(data_); }

    T value_or(T default_value) const {
        return is_ok() ? value() : std::move(default_value);
    }
};

template<typename E>
class Result<void, E> {
public:
    Result() : error_(std::nullopt) {}
    Result(ErrValue<E> err) : error_(std::move(err.error)) {}

    bool is_ok() const { return !error_.has_value(); }
    bool is_error() const { return error_.has_value(); }

    const E& error() const { return error_.value(); }

private:
    std::optional<E> error_;
};

template<typename T>
Result<T> Ok(T value) { return Result<T>(OkValue<T>(std::move(value))); }

template<typename E = std::string>
Result<void, E> Ok() { return Result<void, E>(); }

template<typename T, typename E>
Result<T, E> Err(E error) { return Result<T, E>(ErrValue<E>(std::move(error))); }

} // namespace dfs