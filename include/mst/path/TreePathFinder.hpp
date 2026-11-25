#pragma once

#include "mst/model/MinimumSpanningTree.hpp"

namespace mst {

template <WeightType W> struct PathResult {
    std::vector<VertexId> vertices;
    std::vector<Edge<W>> edges;
    typename WeightTraits<W>::Sum weight{};
};

namespace detail {

class DfsWorkspace {
  public:
    explicit DfsWorkspace(std::size_t vertices)
        : visited_(vertices), predecessors_(vertices), transitions_(vertices) {
        stack_.reserve(vertices);
    }

    template <WeightType W>
    [[nodiscard]] bool search(const CsrIndex& index, std::span<const Edge<W>> edges, VertexId from,
                              VertexId to) {
        return search_edges(
            from, to, [&](VertexId vertex) { return index.incident(vertex); },
            [&](EdgeId id) -> const Edge<W>& { return edges[id.index()]; });
    }

    template <WeightType W>
    [[nodiscard]] bool search(const SpanningTree<W>& tree, VertexId from, VertexId to) {
        return search_edges(
            from, to, [&](VertexId vertex) { return tree.incident(vertex); },
            [&](EdgeId id) -> const Edge<W>& { return tree.edge(id); });
    }

    template <typename Visitor> void unwind(VertexId from, VertexId to, Visitor&& visitor) const {
        for (auto current = to; current != from; current = predecessors_[current.index()]) {
            visitor(current, transitions_[current.index()]);
        }
    }

  private:
    template <typename Incident, typename GetEdge>
    [[nodiscard]] bool search_edges(VertexId from, VertexId to, Incident incident, GetEdge edge) {
        std::ranges::fill(visited_, 0);
        stack_.clear();
        stack_.push_back(from);
        visited_[from.index()] = 1;
        while (!stack_.empty()) {
            const auto vertex = stack_.back();
            stack_.pop_back();
            if (vertex == to) {
                return true;
            }
            for (const auto id : incident(vertex)) {
                const auto next = edge(id).other(vertex);
                if (!visited_[next.index()]) {
                    visited_[next.index()] = 1;
                    predecessors_[next.index()] = vertex;
                    transitions_[next.index()] = id;
                    stack_.push_back(next);
                }
            }
        }
        return false;
    }

    std::vector<unsigned char> visited_;
    std::vector<VertexId> predecessors_;
    std::vector<EdgeId> transitions_;
    std::vector<VertexId> stack_;
};

} // namespace detail

template <WeightType W> struct PathFinder {
    [[nodiscard]] static Result<PathResult<W>> find(const SpanningTree<W>& tree, VertexId from, VertexId to) {
        if (from.index() >= tree.vertex_count() || to.index() >= tree.vertex_count()) {
            return fail(ErrorCode::vertex_out_of_range, "path endpoint is outside 1..n");
        }
        if (from == to) {
            return PathResult<W>{.vertices = {from}, .edges = {}, .weight = {}};
        }
        detail::DfsWorkspace dfs{tree.vertex_count()};
        if (!dfs.search(tree, from, to)) {
            return fail(ErrorCode::invalid_tree, "tree does not connect the requested vertices");
        }
        PathResult<W> path;
        dfs.unwind(from, to, [&](VertexId vertex, EdgeId id) {
            path.vertices.push_back(vertex);
            path.edges.push_back(tree.edge(id));
        });
        path.vertices.push_back(from);
        std::ranges::reverse(path.vertices);
        std::ranges::reverse(path.edges);
        WeightAccumulator<W> total;
        for (const auto& edge : path.edges) {
            if (auto result = total.add(edge.weight); !result) {
                return std::unexpected{std::move(result.error())};
            }
        }
        path.weight = total.value();
        return path;
    }
};

} // namespace mst
