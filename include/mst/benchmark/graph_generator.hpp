#pragma once

#include "mst/model/Graph.hpp"

#include <numeric>
#include <random>

namespace mst {

enum class GraphFamily { chain, star, tree, sparse, dense, complete, equal, signed_weights, disconnected };
enum class RandomEngine { mt19937, philox };

[[nodiscard]] constexpr auto graph_family_names() {
#if MST_HAS_REFLECTION
    return detail::enum_names<GraphFamily>();
#else
    using namespace std::string_view_literals;
    return std::array{"chain"sv,    "star"sv,  "tree"sv,           "sparse"sv,      "dense"sv,
                      "complete"sv, "equal"sv, "signed_weights"sv, "disconnected"sv};
#endif
}

[[nodiscard]] constexpr auto random_engine_names() {
#if MST_HAS_REFLECTION
    return detail::enum_names<RandomEngine>();
#else
    using namespace std::string_view_literals;
    return std::array{"mt19937"sv, "philox"sv};
#endif
}

[[nodiscard]] constexpr std::string_view name(GraphFamily family) {
    return graph_family_names().at(static_cast<std::size_t>(family));
}

[[nodiscard]] constexpr std::string_view name(RandomEngine engine) {
    return random_engine_names().at(static_cast<std::size_t>(engine));
}

template <WeightType W> struct GenerationOptions {
    using Weight = W;
    std::size_t vertices{10};
    std::optional<std::size_t> edges{};
    std::optional<double> density{};
    W low{-100};
    W high{100};
    std::uint64_t seed{42};
    std::size_t memory_limit_bytes{default_memory_limit};
    GraphFamily family{GraphFamily::sparse};
    RandomEngine engine{RandomEngine::mt19937};
};

template <WeightType W> class GraphGenerator {
  public:
    [[nodiscard]] static Result<GraphBuilder<W>> generate(const GenerationOptions<W>& options) {
        const auto count = edge_count(options);
        if (!count) {
            return std::unexpected{count.error()};
        }
        switch (options.engine) {
        case RandomEngine::mt19937: {
            std::mt19937_64 engine{options.seed};
            return generate_with(options, *count, engine);
        }
        case RandomEngine::philox:
#if MST_HAS_PHILOX
        {
            std::philox4x64 engine{options.seed};
            return generate_with(options, *count, engine);
        }
#else
            return fail(ErrorCode::unsupported_engine, "this standard library does not provide Philox");
#endif
        }
        return fail(ErrorCode::invalid_option, "unknown random engine");
    }

  private:
    [[nodiscard]] static Result<std::size_t> edge_count(const GenerationOptions<W>& options) {
        if (options.vertices == 0 || (options.vertices == 1 && options.family == GraphFamily::disconnected)) {
            return fail(ErrorCode::invalid_vertex_count,
                        "disconnected graphs need n >= 2; other families need n >= 1");
        }
        if (options.edges && options.density) {
            return fail(ErrorCode::invalid_option, "specify either edge count or density");
        }
        if (!WeightTraits<W>::finite(options.low) || !WeightTraits<W>::finite(options.high) ||
            options.low > options.high) {
            return fail(ErrorCode::invalid_option, "invalid finite weight range");
        }
        if constexpr (std::floating_point<W>) {
            if (!std::isfinite(static_cast<long double>(options.high) - options.low)) {
                return fail(ErrorCode::invalid_option, "weight interval exceeds long double range");
            }
        }
        const auto total = simple_edge_limit(options.vertices);
        if (total == std::numeric_limits<std::size_t>::max()) {
            return fail(ErrorCode::memory_limit, "edge ranks exceed the supported range");
        }
        const auto tree_edges = options.vertices - 1;
        const auto maximum =
            options.family == GraphFamily::disconnected ? simple_edge_limit(tree_edges) : total;
        const auto minimum = options.family == GraphFamily::disconnected ? 0 : tree_edges;
        auto count = std::min(std::max(minimum, 2 * options.vertices), maximum);
        if (options.family == GraphFamily::dense) {
            count = std::max(minimum, maximum / 2);
        }
        const bool fixed_tree = options.family == GraphFamily::tree || options.family == GraphFamily::chain ||
                                options.family == GraphFamily::star;
        if (fixed_tree) {
            count = minimum;
        }
        if (options.family == GraphFamily::complete) {
            count = maximum;
        }

        const auto density_to_count = [&](double density) -> Result<std::size_t> {
            if (!std::isfinite(density) || density < 0 || density > 1) {
                return fail(ErrorCode::invalid_option, "density must be in [0, 1]");
            }
            return static_cast<std::size_t>(std::round(static_cast<long double>(density) * total));
        };
#if MST_HAS_OPTIONAL_RANGE
        for (const auto density : options.density) {
#else
        if (options.density) {
            const auto density = *options.density;
#endif
            const auto requested = density_to_count(density);
            if (!requested) {
                return std::unexpected{requested.error()};
            }
            count = *requested;
        }
        count = options.edges.value_or(count);

        if (count < minimum || count > maximum || (fixed_tree && count != minimum) ||
            (options.family == GraphFamily::complete && count != maximum)) {
            return fail(ErrorCode::invalid_edge_count, "edge count is incompatible with this graph family");
        }
        if (options.family == GraphFamily::signed_weights &&
            ((count > 0 && (options.low > 0 || options.high < 0)) || (count > 1 && options.low >= 0))) {
            return fail(ErrorCode::invalid_option,
                        "weight range must include zero and, for m >= 2, a negative weight");
        }
        if (!fits_memory_estimate(options.vertices, count, options.memory_limit_bytes, 192, 192)) {
            return fail(ErrorCode::memory_limit,
                        "conservative generation and experiment estimate exceeds the memory limit");
        }
        return count;
    }

    [[nodiscard]] static std::size_t row_begin(std::size_t vertices, std::size_t row) {
        return simple_edge_limit(vertices) - simple_edge_limit(vertices - row);
    }

    [[nodiscard]] static detail::Endpoints unrank(std::size_t vertices, std::size_t rank) {
        std::size_t left = 0;
        auto right = vertices - 1;
        while (right - left > 1) {
            const auto middle = std::midpoint(left, right);
            if (row_begin(vertices, middle) <= rank) {
                left = middle;
            } else {
                right = middle;
            }
        }
        return {VertexId{left}, VertexId{left + 1 + rank - row_begin(vertices, left)}};
    }

    template <std::uniform_random_bit_generator Engine>
    [[nodiscard]] static Result<GraphBuilder<W>> generate_with(const GenerationOptions<W>& options,
                                                               std::size_t count, Engine& engine) {
        const auto random_index = [&](std::size_t upper) {
            return std::uniform_int_distribution<std::size_t>{0, upper}(engine);
        };
        const auto random_weight = [&] {
            if constexpr (std::integral<W>) {
                return std::uniform_int_distribution<W>{options.low, options.high}(engine);
            } else {
                return static_cast<W>(
                    std::uniform_real_distribution<long double>{options.low, options.high}(engine));
            }
        };
        GraphBuilder<W> builder{options.vertices};
        builder.reserve_edges(count);
        const bool disconnected = options.family == GraphFamily::disconnected;
        const auto vertices = options.vertices - static_cast<std::size_t>(disconnected);
        const auto base = disconnected ? 0 : vertices - 1;
        const auto fixed =
            options.family == GraphFamily::equal ? std::optional<W>{random_weight()} : std::nullopt;
        const auto append = [&](std::size_t u, std::size_t v) {
            const auto index = builder.edge_count();
            auto weight = fixed ? *fixed : random_weight();
            if (options.family == GraphFamily::signed_weights) {
                if (index == 0) {
                    weight = 0;
                } else if (index == 1) {
                    weight = options.low;
                }
            }
            return builder.add_edge(VertexId{u}, VertexId{v}, weight);
        };

        std::vector<std::size_t> blocked;
        if (count > base) {
            blocked.reserve(base);
        }
        for (std::size_t v = 1; v <= base; ++v) {
            std::size_t u = 0;
            if (options.family == GraphFamily::chain) {
                u = v - 1;
            } else if (options.family != GraphFamily::star) {
                u = random_index(v - 1);
            }
            if (auto result = append(u, v); !result) {
                return std::unexpected{std::move(result.error())};
            }
            if (count > base) {
                blocked.push_back(row_begin(vertices, u) + v - u - 1);
            }
        }
        if (count == base) {
            return builder;
        }

        std::ranges::sort(blocked);
        const auto available = simple_edge_limit(vertices) - base;
        const auto extra = count - base;
        const bool complement = extra > available / 2;
        const auto samples = std::min(extra, available - extra);
        std::unordered_set<std::size_t> sampled;
        sampled.reserve(samples);
        // Floyd sampling gives a uniform subset without retrying duplicate draws.
        for (auto end = available - samples; end < available; ++end) {
            if (!sampled.insert(random_index(end)).second) {
                sampled.insert(end);
            }
        }
        std::size_t skipped = 0;
        const auto append_missing = [&](std::size_t missing_rank) -> Result<void> {
            auto rank = missing_rank + skipped;
            while (skipped < blocked.size() && blocked[skipped] <= rank) {
                ++rank;
                ++skipped;
            }
            const auto [u, v] = unrank(vertices, rank);
            return append(u.index(), v.index());
        };
        if (complement) {
            for (std::size_t rank = 0; rank < available; ++rank) {
                if (!sampled.contains(rank)) {
                    if (auto result = append_missing(rank); !result) {
                        return std::unexpected{std::move(result.error())};
                    }
                }
            }
        } else {
            std::vector sorted(sampled.begin(), sampled.end());
            std::ranges::sort(sorted);
            for (const auto rank : sorted) {
                if (auto result = append_missing(rank); !result) {
                    return std::unexpected{std::move(result.error())};
                }
            }
        }
        return builder;
    }
};

} // namespace mst
