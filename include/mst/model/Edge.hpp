#pragma once

#include "mst/core/weight.hpp"
#include "mst/utility/features.hpp"

#include <compare>
#include <tuple>

namespace mst {

template <typename Tag> class Identifier {
  public:
    constexpr Identifier() = default;
    explicit constexpr Identifier(std::size_t index) : index_{index} {}
    [[nodiscard]] constexpr std::size_t index() const { return index_; }
    constexpr auto operator<=>(const Identifier&) const = default;

  private:
    std::size_t index_{};
};

using VertexId = Identifier<struct VertexTag>;
using EdgeId = Identifier<struct EdgeTag>;

template <WeightType W> struct Edge {
    VertexId u;
    VertexId v;
    W weight;

    [[nodiscard]] constexpr auto key() const { return std::tuple{weight, u, v}; }
    [[nodiscard]] constexpr VertexId other(const VertexId endpoint) const
#if MST_HAS_CONTRACTS
        pre(endpoint == u || endpoint == v) post(result : result != endpoint)
#endif
    {
        MST_ASSERT(u != v);
#if !MST_HAS_CONTRACTS
        MST_ASSERT(endpoint == u || endpoint == v);
#endif
        return endpoint == u ? v : u;
    }

    constexpr bool operator==(const Edge&) const = default;
};

[[nodiscard]] constexpr std::size_t simple_edge_limit(std::size_t vertices) {
    if (vertices < 2) {
        return 0;
    }
    auto first = vertices;
    auto second = vertices - 1;
    (first % 2 == 0 ? first : second) /= 2;
    constexpr auto max = std::numeric_limits<std::size_t>::max();
    return first > max / second ? max : first * second;
}

inline constexpr std::size_t default_memory_limit = 512U * 1024U * 1024U;

[[nodiscard]] constexpr bool fits_memory_estimate(std::size_t vertices, std::size_t edges, std::size_t limit,
                                                  std::size_t per_vertex, std::size_t per_edge) {
    return vertices <= limit / per_vertex && edges <= (limit - vertices * per_vertex) / per_edge;
}

} // namespace mst
