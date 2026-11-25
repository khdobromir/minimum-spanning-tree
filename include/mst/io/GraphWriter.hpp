#pragma once

#include "mst/io/atomic_output.hpp"
#include "mst/model/Graph.hpp"
#include "mst/utility/text.hpp"

#include <filesystem>
#include <ostream>

namespace mst {

template <WeightType W> struct GraphWriter {
    [[nodiscard]] static Result<void> write(const Graph<W>& graph, std::ostream& output) {
        output << number_text(graph.vertex_count()) << ' ' << number_text(graph.edges().size()) << '\n';
        for (const auto& edge : graph.edges()) {
            output << number_text(edge.u.index() + 1) << ' ' << number_text(edge.v.index() + 1) << ' '
                   << number_text(edge.weight) << '\n';
        }
        if (!output) {
            return fail(ErrorCode::write_error, "cannot write graph");
        }
        return {};
    }

    [[nodiscard]] static Result<void> write(const Graph<W>& graph, const std::filesystem::path& path) {
        return detail::write_file(path, [&](std::ostream& output) { return write(graph, output); });
    }
};

} // namespace mst
