#include "mst/utility/error.hpp"
#include "mst/utility/features.hpp"

#include <algorithm>
#include <format>

namespace mst {

std::string_view error_name(ErrorCode code) {
    using namespace std::string_view_literals;
#if MST_HAS_REFLECTION
    constexpr auto names = detail::enum_names<ErrorCode>();
    static_assert(names.size() == static_cast<std::size_t>(ErrorCode::count) + 1);
#else
    constexpr std::array names{
        "missing_file"sv,        "file_unavailable"sv,  "read_error"sv,           "write_error"sv,
        "empty_file"sv,          "invalid_header"sv,    "invalid_vertex_count"sv, "invalid_edge_count"sv,
        "wrong_record_count"sv,  "extra_data"sv,        "invalid_record"sv,       "invalid_number"sv,
        "number_out_of_range"sv, "nonfinite_weight"sv,  "vertex_out_of_range"sv,  "loop"sv,
        "duplicate_edge"sv,      "weight_overflow"sv,   "disconnected"sv,         "invalid_tree"sv,
        "nonminimal_tree"sv,     "combination_limit"sv, "memory_limit"sv,         "invalid_option"sv,
        "invalid_config"sv,      "unsupported_engine"sv};
    static_assert(names.size() == static_cast<std::size_t>(ErrorCode::count));
#endif
    const auto index = static_cast<std::size_t>(code);
    return index < static_cast<std::size_t>(ErrorCode::count) ? names[index] : "unknown_error"sv;
}

std::string describe(const Error& error) {
    std::string message{error_name(error.code)};
    std::ranges::replace(message, '_', ' ');
    if (!error.detail.empty()) {
        message += std::format(": {}", error.detail);
    }
    if (error.line != 0) {
        message += std::format(" (line {}, column {})", error.line, error.column);
    }
    return message;
}

} // namespace mst
