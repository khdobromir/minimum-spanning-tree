#pragma once

#include "mst/algorithms/KruskalAlgorithm.hpp"
#include "mst/algorithms/PrimAlgorithm.hpp"

namespace mst {

template <WeightType W> [[nodiscard]] std::unique_ptr<IMstAlgorithm<W>> make_algorithm(Algorithm algorithm) {
    switch (algorithm) {
    case Algorithm::prim:
        return std::make_unique<PrimAlgorithm<W>>();
    case Algorithm::kruskal:
        return std::make_unique<KruskalAlgorithm<W>>();
    }
    throw std::invalid_argument{"unknown algorithm"};
}

#if MST_HAS_REFLECTION
template <WeightType W> [[nodiscard]] constexpr std::string_view strategy_type(Algorithm algorithm) {
    constexpr auto prim = std::meta::display_string_of(^^PrimAlgorithm<W>);
    constexpr auto kruskal = std::meta::display_string_of(^^KruskalAlgorithm<W>);
    return algorithm == Algorithm::prim ? prim : kruskal;
}
#endif

} // namespace mst
