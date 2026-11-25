#pragma once

#include "mst/model/Graph.hpp"
#include "mst/utility/text.hpp"

#include <filesystem>
#include <fstream>

#if MST_HAS_INPLACE_VECTOR
#include <inplace_vector>
#endif

namespace mst {

namespace detail {

struct Token {
    std::string_view value;
    std::size_t column;
};

template <std::size_t Size>
[[nodiscard]] Result<std::array<Token, Size>> graph_tokens(std::string_view text, std::size_t line,
                                                           ErrorCode incomplete) {
#if MST_HAS_INPLACE_VECTOR
    std::inplace_vector<Token, Size> tokens;
#else
    std::array<Token, Size> tokens{};
    std::size_t count = 0;
#endif
    for (auto first = text.find_first_not_of(whitespace); first != std::string_view::npos;) {
        const auto end = text.find_first_of(whitespace, first);
        const Token token{text.substr(first, end == std::string_view::npos ? end : end - first), first + 1};
#if MST_HAS_INPLACE_VECTOR
        if (tokens.size() == Size) {
#else
        if (count == Size) {
#endif
            return fail(ErrorCode::extra_data, "too many fields on this line", line, token.column);
        }
#if MST_HAS_INPLACE_VECTOR
        tokens.push_back(token);
#else
        tokens[count++] = token;
#endif
        first = end == std::string_view::npos ? end : text.find_first_not_of(whitespace, end);
    }
#if MST_HAS_INPLACE_VECTOR
    if (tokens.size() != Size) {
#else
    if (count != Size) {
#endif
        return fail(incomplete, "too few fields on this line", line, text.size() + 1);
    }
#if MST_HAS_INPLACE_VECTOR
    std::array<Token, Size> result{};
    std::ranges::copy(tokens, result.begin());
    return result;
#else
    return tokens;
#endif
}

[[nodiscard]] inline Result<std::size_t> nonnegative(Token token, ErrorCode code, std::size_t line) {
    if (token.value.starts_with('-')) {
        return fail(code, "expected a nonnegative integer", line, token.column);
    }
    return parse_number<std::size_t>(token.value, line, token.column);
}

} // namespace detail

template <WeightType W> struct GraphReader {
    [[nodiscard]] static Result<Graph<W>> read(const std::filesystem::path& path,
                                               std::size_t memory_limit = default_memory_limit) {
        return read_draft(path, memory_limit).and_then([](const GraphBuilder<W>& builder) {
            return builder.finish();
        });
    }

    [[nodiscard]] static Result<GraphBuilder<W>> read_draft(const std::filesystem::path& path,
                                                            std::size_t memory_limit = default_memory_limit) {
        std::error_code error;
        const auto status = std::filesystem::status(path, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            return fail(ErrorCode::missing_file, path.string());
        }
        if (error || !std::filesystem::is_regular_file(status)) {
            return fail(ErrorCode::file_unavailable, path.string());
        }
        std::ifstream file{path};
        if (!file.is_open()) {
            return fail(ErrorCode::file_unavailable, path.string());
        }
        auto result = parse(file, memory_limit);
        if (!result) {
            result.error().detail = path.string() + ": " + result.error().detail;
        }
        return result;
    }

    [[nodiscard]] static Result<GraphBuilder<W>> parse(std::istream& input,
                                                       std::size_t memory_limit = default_memory_limit) {
        std::string text;
        if (!std::getline(input, text)) {
            return input.bad() || !input.eof() ? fail(ErrorCode::read_error, "cannot read header", 1, 1)
                                               : fail(ErrorCode::empty_file, "expected graph header", 1, 1);
        }
        const auto header = detail::graph_tokens<2>(text, 1, ErrorCode::invalid_header);
        if (!header) {
            return std::unexpected{header.error()};
        }
        const auto [vertex_token, edge_token] = *header;
        const auto vertices = detail::nonnegative(vertex_token, ErrorCode::invalid_vertex_count, 1);
        if (!vertices) {
            return std::unexpected{vertices.error()};
        }
        if (*vertices == 0) {
            return fail(ErrorCode::invalid_vertex_count, "vertex count must be positive", 1,
                        vertex_token.column);
        }
        const auto count = detail::nonnegative(edge_token, ErrorCode::invalid_edge_count, 1);
        if (!count) {
            return std::unexpected{count.error()};
        }
        if (*count > simple_edge_limit(*vertices)) {
            return fail(ErrorCode::invalid_edge_count, "too many edges for a simple graph", 1,
                        edge_token.column);
        }
        if (!fits_memory_estimate(*vertices, *count, memory_limit, 32, 128)) {
            return fail(ErrorCode::memory_limit, "increase the file memory limit to load this graph", 1, 1);
        }
        GraphBuilder<W> builder{*vertices};
        std::size_t line = 2;
        for (std::size_t i = 0; i < *count; ++i, ++line) {
            if (!std::getline(input, text)) {
                return input.bad() || !input.eof()
                           ? fail(ErrorCode::read_error, "cannot read edge record", line, 1)
                           : fail(ErrorCode::wrong_record_count, "fewer edge records than declared", line, 1);
            }
            const auto fields = detail::graph_tokens<3>(text, line, ErrorCode::invalid_record);
            if (!fields) {
                return std::unexpected{fields.error()};
            }
            const auto [u_token, v_token, weight_token] = *fields;
            const auto u = vertex(u_token, *vertices, line);
            if (!u) {
                return std::unexpected{u.error()};
            }
            const auto v = vertex(v_token, *vertices, line);
            if (!v) {
                return std::unexpected{v.error()};
            }
            const auto weight = parse_number<W>(weight_token.value, line, weight_token.column);
            if (!weight) {
                return std::unexpected{weight.error()};
            }
            if (auto result = builder.add_edge(*u, *v, *weight); !result) {
                auto problem = std::move(result.error());
                problem.line = line;
                problem.column =
                    problem.code == ErrorCode::nonfinite_weight ? weight_token.column : u_token.column;
                return std::unexpected{std::move(problem)};
            }
        }
        for (; std::getline(input, text); ++line) {
            if (const auto offset = text.find_first_not_of(detail::whitespace); offset != std::string::npos) {
                return fail(ErrorCode::extra_data, "more edge records than declared", line, offset + 1);
            }
        }
        if (input.bad() || !input.eof()) {
            return fail(ErrorCode::read_error, "cannot finish reading graph", line, 1);
        }
        return builder;
    }

  private:
    [[nodiscard]] static Result<VertexId> vertex(detail::Token token, std::size_t count, std::size_t line) {
        auto parsed = detail::nonnegative(token, ErrorCode::vertex_out_of_range, line);
        return parsed.and_then([&](std::size_t value) -> Result<VertexId> {
            if (value == 0 || value > count) {
                return fail(ErrorCode::vertex_out_of_range, "vertex must be in 1..n", line, token.column);
            }
            return VertexId{value - 1};
        });
    }
};

} // namespace mst
