#pragma once

#include "mst/model/Edge.hpp"

#include <memory_resource>
#include <ranges>
#include <unordered_set>
#include <vector>

namespace mst {

template <WeightType W> class GraphBuilder;
template <WeightType W> class SpanningTree;

namespace detail {

class CsrIndex {
  public:
    template <WeightType W>
    CsrIndex(std::size_t vertices, std::span<const Edge<W>> edges, std::pmr::memory_resource* storage,
             std::pmr::memory_resource* scratch)
        : offsets_(vertices + 1, storage), adjacency_(2 * edges.size(), storage) {
        for (const auto& edge : edges) {
            ++offsets_[edge.u.index() + 1];
            ++offsets_[edge.v.index() + 1];
        }
        for (std::size_t i = 1; i < offsets_.size(); ++i) {
            offsets_[i] += offsets_[i - 1];
        }
        std::pmr::vector<std::size_t> cursor{offsets_, scratch};
        for (auto [index, edge] : edges | std::views::enumerate) {
            const EdgeId id{static_cast<std::size_t>(index)};
            adjacency_[cursor[edge.u.index()]++] = id;
            adjacency_[cursor[edge.v.index()]++] = id;
        }
    }

    [[nodiscard]] std::size_t vertex_count() const { return offsets_.empty() ? 0 : offsets_.size() - 1; }

    [[nodiscard]] std::span<const EdgeId> incident(VertexId vertex) const {
        const auto offsets = std::span{offsets_}.first(vertex_count());
        const auto begin = span_at(offsets, vertex.index());
        return std::span{adjacency_}.subspan(begin, offsets_[vertex.index() + 1] - begin);
    }

  private:
    std::pmr::vector<std::size_t> offsets_;
    std::pmr::vector<EdgeId> adjacency_;
};

struct Endpoints {
    VertexId u;
    VertexId v;
    bool operator==(const Endpoints&) const = default;
};

struct EndpointsHash {
    std::size_t operator()(Endpoints ends) const noexcept {
        constexpr std::size_t mix = 0x9e3779b97f4a7c15ULL;
        const auto first = ends.u.index();
        return first ^ (ends.v.index() + mix + (first << 6) + (first >> 2));
    }
};

} // namespace detail

template <WeightType W> class Graph {
  public:
    Graph(Graph&&) noexcept = default;
    Graph(const Graph&) = default;
    Graph& operator=(const Graph&) = delete;
    Graph& operator=(Graph&&) = delete;

    [[nodiscard]] std::size_t vertex_count() const { return adjacency_.vertex_count(); }
    [[nodiscard]] std::span<const Edge<W>> edges() const& { return edges_; }
    std::span<const Edge<W>> edges() const&& = delete;
    [[nodiscard]] const Edge<W>& edge(EdgeId id) const& { return detail::span_at(edges(), id.index()); }
    const Edge<W>& edge(EdgeId) const&& = delete;
    [[nodiscard]] std::span<const EdgeId> incident(VertexId vertex) const& {
        return adjacency_.incident(vertex);
    }
    std::span<const EdgeId> incident(VertexId) const&& = delete;

  private:
    friend class GraphBuilder<W>;
    friend class SpanningTree<W>;

    Graph(std::size_t vertices, std::pmr::vector<Edge<W>> edges, std::pmr::memory_resource* scratch)
        : edges_{std::move(edges)},
          adjacency_{vertices, std::span<const Edge<W>>{edges_}, edges_.get_allocator().resource(), scratch} {
    }

    std::pmr::vector<Edge<W>> edges_;
    detail::CsrIndex adjacency_;
};

template <WeightType W> class GraphBuilder {
  public:
    explicit GraphBuilder(std::size_t vertices) : vertices_{vertices} {}

    [[nodiscard]] Result<void> add_edge(VertexId u, VertexId v, W weight) {
        return add_edge({.u = u, .v = v, .weight = weight});
    }

    [[nodiscard]] Result<void> add_edge(Edge<W> edge) {
        if (edge.u.index() >= vertices_ || edge.v.index() >= vertices_) {
            return fail(ErrorCode::vertex_out_of_range, "edge endpoint is outside 1..n");
        }
        if (edge.u == edge.v) {
            return fail(ErrorCode::loop, "an edge must connect two different vertices");
        }
        if (!WeightTraits<W>::finite(edge.weight)) {
            return fail(ErrorCode::nonfinite_weight, "edge weight must be finite");
        }
        if (edge.v < edge.u) {
            std::swap(edge.u, edge.v);
        }
        if constexpr (std::floating_point<W>) {
            if (edge.weight == 0) {
                edge.weight = 0; // Canonicalize negative zero for stable export.
            }
        }
        const auto [entry, inserted] = seen_.insert({edge.u, edge.v});
        if (!inserted) {
            return fail(ErrorCode::duplicate_edge, "unordered pair of endpoints already exists");
        }
        try {
            edges_.push_back(edge);
        } catch (...) {
            seen_.erase(entry);
            throw;
        }
        return {};
    }

    void reserve_edges(std::size_t count) {
        edges_.reserve(count);
        seen_.reserve(count);
    }

    [[nodiscard]] std::size_t vertex_count() const { return vertices_; }
    [[nodiscard]] std::size_t edge_count() const { return edges_.size(); }

    // A custom storage resource must outlive the returned graph.
    [[nodiscard]] Result<Graph<W>>
    finish(std::pmr::memory_resource* storage = std::pmr::get_default_resource()) const {
        if (vertices_ == 0) {
            return fail(ErrorCode::invalid_vertex_count, "a graph needs at least one vertex");
        }
        if (vertices_ >= std::pmr::vector<std::size_t>{}.max_size() ||
            edges_.size() > std::pmr::vector<EdgeId>{}.max_size() / 2) {
            return fail(ErrorCode::memory_limit, "graph representation exceeds container limits");
        }
        return Graph<W>{vertices_, std::pmr::vector<Edge<W>>{edges_.begin(), edges_.end(), storage}, storage};
    }

  private:
    std::size_t vertices_;
    std::vector<Edge<W>> edges_;
    std::unordered_set<detail::Endpoints, detail::EndpointsHash> seen_;
};

} // namespace mst
