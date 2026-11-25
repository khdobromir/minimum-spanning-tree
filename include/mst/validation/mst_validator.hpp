#pragma once

#include "mst/algorithms/disjoint_set_union.hpp"
#include "mst/path/TreePathFinder.hpp"

#include <numeric>
#include <unordered_map>

namespace mst {

template <WeightType W> class MstValidator {
  public:
    using Sum = WeightTraits<W>::Sum;

    [[nodiscard]] static Result<void> validate(const Graph<W>& graph, const SpanningTree<W>& tree,
                                               bool check_minimality = true) {
        return validate(graph, tree.vertex_count(), tree.edges(), tree.weight(), check_minimality);
    }

    [[nodiscard]] static Result<void> validate(const Graph<W>& graph, std::size_t vertices,
                                               std::span<const Edge<W>> candidate, Sum reported,
                                               bool check_minimality = true) {
        if (vertices == 0 || vertices != graph.vertex_count() || candidate.size() != vertices - 1) {
            return fail(ErrorCode::invalid_tree, "the candidate must cover n vertices with n-1 edges");
        }
        std::unordered_map<detail::Endpoints, W, detail::EndpointsHash> remaining;
        remaining.reserve(graph.edges().size());
        for (const auto& edge : graph.edges()) {
            remaining.emplace(detail::Endpoints{edge.u, edge.v}, edge.weight);
        }
        DisjointSetUnion sets{vertices};
        for (const auto& edge : candidate) {
            const auto found = remaining.find({std::min(edge.u, edge.v), std::max(edge.u, edge.v)});
            if (found == remaining.end() || found->second != edge.weight) {
                return fail(ErrorCode::invalid_tree, "unknown, repeated or incorrectly weighted edge");
            }
            remaining.erase(found);
            if (!sets.unite(edge.u, edge.v)) {
                return fail(ErrorCode::invalid_tree, "the candidate contains a cycle");
            }
        }
        if (sets.component_count() != 1) {
            return fail(ErrorCode::invalid_tree, "the candidate is disconnected");
        }

        std::vector ordered(candidate.begin(), candidate.end());
        std::ranges::sort(ordered, {}, [](const Edge<W>& edge) {
            return std::tuple{edge.weight, std::min(edge.u, edge.v), std::max(edge.u, edge.v)};
        });
        const auto weight = recalculate(ordered);
        if (!weight) {
            return std::unexpected{weight.error()};
        }
        if (!WeightTraits<W>::equivalent(*weight, reported)) {
            return fail(ErrorCode::invalid_tree, "reported total does not match the edge weights");
        }
        if (!check_minimality || remaining.empty()) {
            return {};
        }

        auto* const resource = std::pmr::get_default_resource();
        const detail::CsrIndex index{vertices, candidate, resource, resource};
        detail::DfsWorkspace dfs{vertices};
        // Iterate in input order so the first exchange failure is reproducible.
        for (const auto& edge : graph.edges()) {
            if (!remaining.contains({edge.u, edge.v})) {
                continue;
            }
            if (!dfs.search(index, candidate, edge.u, edge.v)) {
                return fail(ErrorCode::invalid_tree, "could not find a path inside the candidate");
            }
            auto maximum = std::numeric_limits<W>::lowest();
            dfs.unwind(edge.u, edge.v, [&](VertexId, EdgeId id) {
                maximum = std::max(maximum, candidate[id.index()].weight);
            });
            if (edge.weight < maximum) {
                return fail(ErrorCode::nonminimal_tree,
                            "a lighter edge can replace an edge on its tree path");
            }
        }
        return {};
    }

    [[nodiscard]] static Result<Sum> brute_force(const Graph<W>& graph, std::size_t limit = 100'000) {
        const auto vertices = graph.vertex_count();
        if (vertices == 0 || graph.edges().size() < vertices - 1) {
            return fail(ErrorCode::disconnected, "the graph has no spanning tree");
        }
        const auto choose = vertices - 1;
        const auto combinations = combination_count(graph.edges().size(), choose, limit);
        if (!combinations) {
            return fail(ErrorCode::combination_limit, "too many candidate edge subsets");
        }
        if (choose == 0) {
            return Sum{};
        }
        // This ordering is independent of the algorithm's acceptance order.
        std::vector ordered(graph.edges().begin(), graph.edges().end());
        std::ranges::sort(ordered, {}, &Edge<W>::key);
        std::vector<std::size_t> selection(choose);
        std::ranges::copy(std::views::iota(std::size_t{0}, choose), selection.begin());
        using ReferenceSum = std::conditional_t<std::integral<W>, detail::ExactIntegerSum, Sum>;
        std::optional<ReferenceSum> best;
        std::vector<std::size_t> best_selection;
        for (std::size_t iteration = 0; iteration < *combinations; ++iteration) {
            DisjointSetUnion sets{vertices};
            const auto acyclic = std::ranges::all_of(selection, [&](std::size_t selected) {
                return sets.unite(ordered[selected].u, ordered[selected].v);
            });
            if (acyclic) {
                ReferenceSum weight;
                if constexpr (std::integral<W>) {
                    for (const auto selected : selection) {
                        weight.add(ordered[selected].weight);
                    }
                } else {
                    WeightAccumulator<W> total;
                    for (const auto selected : selection) {
                        if (auto result = total.add(ordered[selected].weight); !result) {
                            return std::unexpected{std::move(result.error())};
                        }
                    }
                    weight = total.value();
                }
                if (!best || weight < *best) {
                    best = weight;
                    if constexpr (std::integral<W>) {
                        best_selection = selection;
                    }
                }
            }
            if (iteration != *combinations - 1) {
                auto position = choose - 1;
                while (selection[position] == ordered.size() - choose + position) {
                    --position;
                }
                ++selection[position];
                for (auto next = position + 1; next < choose; ++next) {
                    selection[next] = selection[next - 1] + 1;
                }
            }
        }
        if (!best) {
            return fail(ErrorCode::disconnected, "no spanning subset exists");
        }
        if constexpr (std::integral<W>) {
            WeightAccumulator<W> total;
            for (const auto selected : best_selection) {
                if (auto result = total.add(ordered[selected].weight); !result) {
                    return std::unexpected{std::move(result.error())};
                }
            }
            return total.value();
        } else {
            return *best;
        }
    }

  private:
    [[nodiscard]] static Result<Sum> recalculate(std::span<const Edge<W>> ordered) {
        WeightAccumulator<W> total;
        for (const auto& edge : ordered) {
            if (auto result = total.add(edge.weight); !result) {
                return std::unexpected{std::move(result.error())};
            }
        }
        return total.value();
    }

    [[nodiscard]] static std::optional<std::size_t> combination_count(std::size_t count, std::size_t choose,
                                                                      std::size_t limit) {
        choose = std::min(choose, count - choose);
        auto result = std::size_t{1};
        if (limit == 0) {
            return std::nullopt;
        }
        for (std::size_t i = 1; i <= choose; ++i) {
            const auto term = count - choose + i;
            const auto common = std::gcd(term, i);
            const auto denominator = i / common;
            const auto numerator = term / common;
            // The reduced denominator divides the previous binomial coefficient exactly.
            MST_ASSERT(numerator > 0 && denominator > 0 && result % denominator == 0);
            const auto reduced = result / denominator;
            if (reduced > limit / numerator) {
                return std::nullopt;
            }
            result = reduced * numerator;
        }
        return result;
    }
};

} // namespace mst
