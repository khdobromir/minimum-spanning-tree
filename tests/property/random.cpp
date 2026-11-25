#include "mst/algorithms/strategies.hpp"
#include "mst/benchmark/graph_generator.hpp"
#include "mst/validation/mst_validator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace mst;
using Catch::Generators::table;

TEST_CASE("Generated small graphs match enumeration regardless of starting vertex or input order",
          "[property]") {
    using I = std::int64_t;
    const auto family = GENERATE(GraphFamily::sparse, GraphFamily::equal, GraphFamily::signed_weights);
    for (std::uint64_t seed = 0; seed < 25; ++seed) {
        CAPTURE(seed, static_cast<int>(family));
        const GenerationOptions<I> options{.vertices = 6, .edges = 9, .seed = seed, .family = family};
        const auto draft = GraphGenerator<I>::generate(options);
        REQUIRE(draft);
        const auto graph = draft->finish();
        REQUIRE(graph);
        const auto expected = MstValidator<I>::brute_force(*graph, 126);
        REQUIRE(expected);
        GraphBuilder<I> reversed{6};
        for (const auto& edge : graph->edges() | std::views::reverse) {
            REQUIRE(reversed.add_edge(edge.v, edge.u, edge.weight));
        }
        const auto other = reversed.finish();
        REQUIRE(other);
        const auto kruskal = KruskalAlgorithm<I>{}.build(*graph);
        REQUIRE(kruskal);
        CHECK(kruskal->weight() == *expected);
        REQUIRE(MstValidator<I>::validate(*graph, *kruskal));
        for (std::size_t start = 0; start < 6; ++start) {
            const auto prim = PrimAlgorithm<I>{}.build(*graph, {.start = VertexId{start}});
            const auto repeated = PrimAlgorithm<I>{}.build(*other, {.start = VertexId{start}});
            REQUIRE(prim);
            REQUIRE(repeated);
            CHECK(prim->weight() == *expected);
            CHECK(std::ranges::equal(prim->edges(), repeated->edges()));
            REQUIRE(MstValidator<I>::validate(*graph, *prim));
        }
    }
}

TEST_CASE("Generator handles complete, near-complete, and disconnected graphs", "[generator]") {
    using I = std::int64_t;
    const auto [family, expected] =
        GENERATE(table<GraphFamily, std::size_t>({{GraphFamily::chain, 14},
                                                  {GraphFamily::star, 14},
                                                  {GraphFamily::tree, 14},
                                                  {GraphFamily::complete, 105},
                                                  {GraphFamily::dense, 104},
                                                  {GraphFamily::disconnected, 91}}));
    const GenerationOptions<I> options{.vertices = 15, .edges = expected, .seed = 73, .family = family};
    const auto draft = GraphGenerator<I>::generate(options);
    const auto copy = GraphGenerator<I>::generate(options);
    REQUIRE(draft);
    REQUIRE(copy);
    const auto graph = draft->finish();
    const auto other = copy->finish();
    REQUIRE(graph);
    REQUIRE(other);
    CHECK(graph->edges().size() == expected);
    CHECK(std::ranges::equal(graph->edges(), other->edges()));
    const auto tree = PrimAlgorithm<I>{}.build(*graph);
    CHECK(tree.has_value() == (family != GraphFamily::disconnected));
}

TEST_CASE("Generation rejects an estimated memory budget violation", "[generator]") {
    const GenerationOptions<double> options{.vertices = 100'000, .edges = 100'000, .memory_limit_bytes = 64};
    const auto graph = GraphGenerator<double>::generate(options);
    REQUIRE_FALSE(graph);
    CHECK(graph.error().code == ErrorCode::memory_limit);
}

TEST_CASE("Philox generates reproducible floating graphs when available", "[generator][property]") {
    const GenerationOptions<double> options{
        .vertices = 6, .edges = 9, .low = -0.5, .high = 0.25, .engine = RandomEngine::philox};
    const auto first = GraphGenerator<double>::generate(options);
#if MST_HAS_PHILOX
    const auto repeat = GraphGenerator<double>::generate(options);
    REQUIRE(first);
    REQUIRE(repeat);
    const auto graph = first->finish();
    const auto other = repeat->finish();
    REQUIRE(graph);
    REQUIRE(other);
    CHECK(std::ranges::equal(graph->edges(), other->edges()));
    const auto prim = PrimAlgorithm<double>{}.build(*graph);
    const auto kruskal = KruskalAlgorithm<double>{}.build(*graph);
    const auto minimum = MstValidator<double>::brute_force(*graph);
    REQUIRE(prim);
    REQUIRE(kruskal);
    REQUIRE(minimum);
    CHECK(WeightTraits<double>::equivalent(prim->weight(), *minimum));
    CHECK(WeightTraits<double>::equivalent(kruskal->weight(), *minimum));
    CHECK(MstValidator<double>::validate(*graph, *prim));
    CHECK(MstValidator<double>::validate(*graph, *kruskal));
#else
    REQUIRE_FALSE(first);
    CHECK(first.error().code == ErrorCode::unsupported_engine);
#endif
}
