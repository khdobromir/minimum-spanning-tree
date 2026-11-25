#pragma once

#include "mst/utility/error.hpp"

#include <array>
#include <charconv>
#include <concepts>
#include <stdexcept>

namespace mst {

template <typename T>
concept Number =
    (std::integral<T> || std::floating_point<T>) &&
    requires(const char* first, const char* last, T& value) { std::from_chars(first, last, value); };

template <Number T>
[[nodiscard]] Result<T> parse_number(std::string_view token, std::size_t line = 0, std::size_t column = 0) {
    if (token.starts_with('+')) {
        token.remove_prefix(1);
        if (token.starts_with('-') || token.starts_with('+')) {
            return fail(ErrorCode::invalid_number, "unexpected second sign", line, column);
        }
    }
    if (token.empty()) {
        return fail(ErrorCode::invalid_number, "expected a numeric token", line, column);
    }
    T value{};
    const auto* end = token.data() + token.size();
    const auto [position, error] = std::from_chars(token.data(), end, value);
    if (error == std::errc::result_out_of_range) {
        return fail(ErrorCode::number_out_of_range, std::string{token}, line, column);
    }
    if (error != std::errc{} || position != end) {
        return fail(ErrorCode::invalid_number, std::string{token}, line, column);
    }
    return value;
}

template <Number T> [[nodiscard]] std::string number_text(T value) {
    std::array<char, 128> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) {
        throw std::logic_error{"numeric output exceeds its buffer"};
    }
    return {buffer.data(), end};
}

namespace detail {

inline constexpr std::string_view whitespace = " \t\r\n\f\v";

} // namespace detail
} // namespace mst
