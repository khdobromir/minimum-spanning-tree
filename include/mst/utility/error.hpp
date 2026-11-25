#pragma once

#include <cstddef>
#include <expected>
#include <source_location>
#include <string>
#include <string_view>

namespace mst {

enum class ErrorCode {
    missing_file,
    file_unavailable,
    read_error,
    write_error,
    empty_file,
    invalid_header,
    invalid_vertex_count,
    invalid_edge_count,
    wrong_record_count,
    extra_data,
    invalid_record,
    invalid_number,
    number_out_of_range,
    nonfinite_weight,
    vertex_out_of_range,
    loop,
    duplicate_edge,
    weight_overflow,
    disconnected,
    invalid_tree,
    nonminimal_tree,
    combination_limit,
    memory_limit,
    invalid_option,
    invalid_config,
    unsupported_engine,
    count
};

struct Error {
    ErrorCode code;
    std::string detail;
    std::size_t line{};
    std::size_t column{};
    std::source_location origin;
};

template <typename T> using Result = std::expected<T, Error>;

[[nodiscard]] inline auto fail(ErrorCode code, std::string detail = {}, std::size_t line = 0,
                               std::size_t column = 0,
                               std::source_location origin = std::source_location::current()) {
    return std::unexpected(
        Error{.code = code, .detail = std::move(detail), .line = line, .column = column, .origin = origin});
}

[[nodiscard]] std::string_view error_name(ErrorCode code);
[[nodiscard]] std::string describe(const Error& error);

} // namespace mst
