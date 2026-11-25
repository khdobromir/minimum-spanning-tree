#pragma once

#include "mst/algorithms/MstAlgorithm.hpp"

namespace mst {

template <WeightType W> class KruskalAlgorithm final : public IMstAlgorithm<W> {
  public:
    [[nodiscard]] std::string_view name() const noexcept override {
#if MST_HAS_REFLECTION
        return detail::enum_names<Algorithm>()[std::to_underlying(Algorithm::kruskal)];
#else
        return "kruskal";
#endif
    }

    [[nodiscard]] Result<SpanningTree<W>> build(const Graph<W>& graph,
                                                BuildOptions<W> options = {}) const override {
        const auto vertices = graph.vertex_count();
        if (vertices == 0) {
            return fail(ErrorCode::invalid_vertex_count, "the graph has no vertices");
        }
        const auto required = vertices - 1;
        DisjointSetUnion sets{vertices, options.scratch};
        std::pmr::vector<EdgeId> order(graph.edges().size(), options.scratch);
        std::ranges::transform(std::views::iota(std::size_t{0}, order.size()), order.begin(),
                               [](std::size_t index) { return EdgeId{index}; });
        std::ranges::sort(order, {}, [&](EdgeId id) { return graph.edge(id).key(); });
        std::pmr::vector<EdgeId> accepted{options.scratch};
        accepted.reserve(required);
        detail::TraceProgress<W> progress;

        for (const auto id : order) {
            const auto& edge = graph.edge(id);
            const auto joined = sets.unite(edge.u, edge.v);
            if (joined) {
                accepted.push_back(id);
            }
            if (options.trace) {
                (*options.trace)(KruskalStep<W>{
                    progress.step(edge, joined ? Decision::accepted : Decision::cycle, accepted.size()),
                    sets.component_count()});
            }
            if (accepted.size() == required) {
                break;
            }
        }
        return SpanningTree<W>::complete(graph, accepted, options.scratch, options.storage);
    }
};

} // namespace mst
