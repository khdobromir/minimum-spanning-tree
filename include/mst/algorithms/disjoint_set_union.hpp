#pragma once

#include "mst/model/Edge.hpp"

#include <memory_resource>
#include <ranges>
#include <vector>

namespace mst {

class DisjointSetUnion {
  public:
    explicit DisjointSetUnion(std::size_t count,
                              std::pmr::memory_resource* memory = std::pmr::get_default_resource())
        : parents_(count, memory), sizes_(count, 1, memory), components_{count} {
        std::ranges::transform(std::views::iota(std::size_t{0}, count), parents_.begin(),
                               [](std::size_t i) { return VertexId{i}; });
    }

    [[nodiscard]] VertexId find(VertexId vertex) {
        auto root = vertex;
        while (parents_.at(root.index()) != root) {
            root = parents_[root.index()];
        }
        while (vertex != root) {
            auto& parent = parents_[vertex.index()];
            vertex = std::exchange(parent, root);
        }
        return root;
    }

    [[nodiscard]] bool unite(VertexId first, VertexId second) {
        auto root = find(first);
        auto child = find(second);
        if (root == child) {
            return false;
        }
        if (sizes_[root.index()] < sizes_[child.index()]) {
            std::swap(root, child);
        }
        parents_[child.index()] = root;
        sizes_[root.index()] += sizes_[child.index()];
        --components_;
        return true;
    }

    [[nodiscard]] bool connected(VertexId first, VertexId second) { return find(first) == find(second); }
    [[nodiscard]] std::size_t component_count() const { return components_; }

  private:
    std::pmr::vector<VertexId> parents_;
    std::pmr::vector<std::size_t> sizes_;
    std::size_t components_;
};

} // namespace mst
