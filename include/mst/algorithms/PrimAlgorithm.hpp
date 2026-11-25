#pragma once

#include "mst/algorithms/MstAlgorithm.hpp"

#include <queue>

namespace mst {

template <WeightType W> class PrimAlgorithm final : public IMstAlgorithm<W> {
  public:
    [[nodiscard]] std::string_view name() const noexcept override {
#if MST_HAS_REFLECTION
        return detail::enum_names<Algorithm>()[std::to_underlying(Algorithm::prim)];
#else
        return "prim";
#endif
    }

    [[nodiscard]] Result<SpanningTree<W>> build(const Graph<W>& graph,
                                                BuildOptions<W> options = {}) const override {
        const auto vertices = graph.vertex_count();
        if (options.start.index() >= vertices) {
            return fail(ErrorCode::vertex_out_of_range, "Prim start is outside 1..n");
        }
        const auto required = vertices - 1;
        std::pmr::vector<EdgeId> accepted{options.scratch};
        accepted.reserve(required);
        std::pmr::vector<unsigned char> included(vertices, options.scratch);
        const auto heavier = [&](EdgeId a, EdgeId b) { return graph.edge(a).key() > graph.edge(b).key(); };
        std::priority_queue queue{heavier, std::pmr::vector<EdgeId>{options.scratch}};
        detail::TraceProgress<W> progress;

        const auto include = [&](VertexId vertex) {
            included[vertex.index()] = 1;
            for (const auto id : graph.incident(vertex)) {
                if (!included[graph.edge(id).other(vertex).index()]) {
                    queue.push(id);
                }
            }
        };
        include(options.start);

        while (accepted.size() < required && !queue.empty()) {
            const auto id = queue.top();
            queue.pop();
            const auto& edge = graph.edge(id);
            MST_ASSERT(included[edge.u.index()] || included[edge.v.index()]);
            const auto next = included[edge.u.index()] ? edge.v : edge.u;
            auto decision = Decision::already_included;
            if (!included[next.index()]) {
                decision = Decision::accepted;
                accepted.push_back(id);
                include(next);
            }
            if (options.trace) {
                (*options.trace)(
                    PrimStep<W>{progress.step(edge, decision, accepted.size()), accepted.size() + 1});
            }
        }
        return SpanningTree<W>::complete(graph, accepted, options.scratch, options.storage);
    }
};

} // namespace mst
