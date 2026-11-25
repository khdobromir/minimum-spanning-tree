#pragma once

#include "mst/model/Graph.hpp"

namespace mst {

template <WeightType W> class PrimAlgorithm;
template <WeightType W> class KruskalAlgorithm;

template <WeightType W> class SpanningTree {
  public:
    using Sum = WeightTraits<W>::Sum;

    [[nodiscard]] std::size_t vertex_count() const { return tree_.vertex_count(); }
    [[nodiscard]] Sum weight() const { return weight_; }
    [[nodiscard]] std::span<const Edge<W>> edges() const& { return tree_.edges(); }
    std::span<const Edge<W>> edges() const&& = delete;
    [[nodiscard]] const Edge<W>& edge(EdgeId id) const& { return tree_.edge(id); }
    const Edge<W>& edge(EdgeId) const&& = delete;
    [[nodiscard]] std::span<const EdgeId> incident(VertexId vertex) const& { return tree_.incident(vertex); }
    std::span<const EdgeId> incident(VertexId) const&& = delete;

  private:
    friend class PrimAlgorithm<W>;
    friend class KruskalAlgorithm<W>;

    SpanningTree(Graph<W> tree, Sum weight) : tree_{std::move(tree)}, weight_{weight} {}

    [[nodiscard]] static Result<SpanningTree> complete(const Graph<W>& graph,
                                                       std::span<const EdgeId> accepted,
                                                       std::pmr::memory_resource* scratch,
                                                       std::pmr::memory_resource* storage) {
        const auto vertices = graph.vertex_count();
        if (vertices == 0 || accepted.size() != vertices - 1) {
            return fail(ErrorCode::disconnected, "the graph has no spanning tree");
        }
        std::pmr::vector<EdgeId> sorted(accepted.begin(), accepted.end(), scratch);
        std::ranges::sort(sorted, {}, [&](EdgeId id) { return graph.edge(id).key(); });
        WeightAccumulator<W> weight;
        for (const auto id : sorted) {
            if (auto result = weight.add(graph.edge(id).weight); !result) {
                return std::unexpected{std::move(result.error())};
            }
        }
        std::pmr::vector<Edge<W>> edges{storage};
        edges.reserve(accepted.size());
        std::ranges::transform(accepted, std::back_inserter(edges),
                               [&](EdgeId id) { return graph.edge(id); });
        return SpanningTree{Graph<W>{vertices, std::move(edges), scratch}, weight.value()};
    }

    Graph<W> tree_;
    Sum weight_;
};

} // namespace mst
