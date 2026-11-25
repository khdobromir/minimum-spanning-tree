#include "mst/algorithms/strategies.hpp"
#include "mst/io/GraphFileReader.hpp"
#include "mst/io/GraphWriter.hpp"
#include "mst/path/TreePathFinder.hpp"
#include "mst/utility/tracking_memory.hpp"
#include "mst/validation/mst_validator.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <sstream>

using namespace mst;
using Catch::Generators::table;
using I = std::int64_t;
using E = Edge<I>;
using V = VertexId;

static_assert(WeightType<I> && WeightType<double> && !WeightType<float>);
static_assert(!std::is_convertible_v<VertexId, EdgeId>);
static_assert(!std::is_convertible_v<std::size_t, VertexId>);
static_assert(simple_edge_limit(6) == 15);
static_assert(checked_add(2, 3) == 5);
static_assert(!checked_add(std::numeric_limits<I>::max(), 1));

TEST_CASE("DSU compresses paths and merges components once", "[dsu]") {
    DisjointSetUnion dsu{8};
    REQUIRE(dsu.component_count() == 8);
    REQUIRE(dsu.unite(V{0}, V{1}));
    REQUIRE(dsu.unite(V{2}, V{3}));
    REQUIRE(dsu.unite(V{1}, V{3}));
    REQUIRE(dsu.unite(V{4}, V{5}));
    CHECK(dsu.find(V{0}) == dsu.find(V{3}));
    CHECK_FALSE(dsu.connected(V{3}, V{4}));
    CHECK_FALSE(dsu.unite(V{3}, V{0}));
    CHECK(dsu.component_count() == 4);
}

TEST_CASE("Chapter one matches both published traces and the unique path", "[chapter1]") {
    const auto graph = GraphReader<I>::read(std::filesystem::path{MST_EXAMPLES} / "chapter1.txt");
    REQUIRE(graph);
    const auto algorithm = GENERATE(Algorithm::prim, Algorithm::kruskal);
    bool rejected_cycle = false;
    auto trace = [&](const TraceEvent<I>& event) {
        std::visit(
            [&](const auto& step) {
                CHECK(step.common.subtotal);
                if (step.common.edge == E{V{2}, V{3}, 2}) {
                    rejected_cycle = step.common.decision == Decision::cycle;
                }
            },
            event);
    };
    const auto tree = make_algorithm<I>(algorithm)->build(*graph, {.trace = trace});
    REQUIRE(tree);
    const std::vector prim{E{V{0}, V{2}, 3}, E{V{1}, V{2}, 1}, E{V{1}, V{3}, 2}, E{V{3}, V{4}, 2},
                           E{V{4}, V{5}, 3}};
    const std::vector kruskal{prim[1], prim[2], prim[3], prim[0], prim[4]};
    CHECK(std::ranges::equal(tree->edges(), algorithm == Algorithm::prim ? prim : kruskal));
    if (algorithm == Algorithm::kruskal) {
        CHECK(rejected_cycle);
    }
    CHECK(tree->weight() == 11);
    CHECK(MstValidator<I>::validate(*graph, *tree));
    const auto path = PathFinder<I>::find(*tree, V{0}, V{5});
    REQUIRE(path);
    CHECK(path->vertices == std::vector{V{0}, V{2}, V{1}, V{3}, V{4}, V{5}});
    CHECK(path->weight == 11);
    const auto indirect = PathFinder<I>::find(*tree, V{0}, V{1});
    REQUIRE(indirect);
    CHECK(indirect->edges.size() == 2);
    CHECK(graph->edge(EdgeId{0}).weight == indirect->weight);
    CHECK(PathFinder<I>::find(*tree, V{0}, V{6}).error().code == ErrorCode::vertex_out_of_range);
}

TEST_CASE("Tree paths need not be shortest in the original graph", "[path]") {
    GraphBuilder<I> builder{3};
    REQUIRE(builder.add_edge(V{0}, V{1}, 2));
    REQUIRE(builder.add_edge(V{1}, V{2}, 2));
    REQUIRE(builder.add_edge(V{0}, V{2}, 3));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto tree = KruskalAlgorithm<I>{}.build(*graph);
    REQUIRE(tree);
    const auto path = PathFinder<I>::find(*tree, V{0}, V{2});
    REQUIRE(path);
    CHECK(path->weight == 4);
    CHECK(graph->edge(EdgeId{2}).weight == 3);
}

TEST_CASE("Small paths include singleton, negative and zero edges", "[mst][path]") {
    const auto count = GENERATE(1U, 2U, 3U);
    const auto algorithm = GENERATE(Algorithm::prim, Algorithm::kruskal);
    GraphBuilder<I> builder{count};
    if (count > 1) {
        REQUIRE(builder.add_edge(E{V{1}, V{0}, -7}));
    }
    if (count > 2) {
        REQUIRE(builder.add_edge(V{1}, V{2}, 0));
    }
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto strategy = make_algorithm<I>(algorithm);
    for (int repeat = 0; repeat < 2; ++repeat) {
        const auto tree = strategy->build(*graph);
        REQUIRE(tree);
        CHECK(tree->edges().size() == count - 1);
        CHECK(tree->weight() == (count > 1 ? -7 : 0));
        const auto path = PathFinder<I>::find(*tree, V{count - 1}, V{0});
        REQUIRE(path);
        CHECK(path->vertices.size() == count);
        CHECK(path->weight == tree->weight());
        const auto single = PathFinder<I>::find(*tree, V{0}, V{0});
        REQUIRE(single);
        CHECK(single->vertices == std::vector{V{0}});
        CHECK(single->edges.empty());
        CHECK(single->weight == 0);
    }
}

TEST_CASE("Input errors retain their type and location", "[io]") {
    const auto [input, code, line] = GENERATE(table<std::string, ErrorCode, std::size_t>({
        {"", ErrorCode::empty_file, 1},
        {"3", ErrorCode::invalid_header, 1},
        {"0 0", ErrorCode::invalid_vertex_count, 1},
        {"-1 0", ErrorCode::invalid_vertex_count, 1},
        {"3 -1", ErrorCode::invalid_edge_count, 1},
        {"3 4", ErrorCode::invalid_edge_count, 1},
        {"3 1", ErrorCode::wrong_record_count, 2},
        {"3 1\n1 2\n", ErrorCode::invalid_record, 2},
        {"3 1\n1 2 4 5\n", ErrorCode::extra_data, 2},
        {"3 0\n1 2 4\n", ErrorCode::extra_data, 2},
        {"3 1\n0 2 4\n", ErrorCode::vertex_out_of_range, 2},
        {"3 1\n1 4 4\n", ErrorCode::vertex_out_of_range, 2},
        {"3 1\n1 1 4\n", ErrorCode::loop, 2},
        {"3 2\n1 2 4\n1 2 5\n", ErrorCode::duplicate_edge, 3},
        {"3 2\n1 2 4\n2 1 5\n", ErrorCode::duplicate_edge, 3},
        {"3 1\n1 2 4x\n", ErrorCode::invalid_number, 2},
        {"3 1\n1 2 99999999999999999999999\n", ErrorCode::number_out_of_range, 2},
    }));
    std::istringstream stream{input};
    const auto result = GraphReader<I>::parse(stream);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == code);
    CHECK(result.error().line == line);
    CHECK(result.error().column > 0);
}

TEST_CASE("Floating weights reject infinities and NaNs", "[io][weights]") {
    const auto weight = GENERATE("nan", "-inf", "+infinity");
    std::istringstream stream{std::string{"2 1\n1 2 "} + weight};
    const auto result = GraphReader<double>::parse(stream);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == ErrorCode::nonfinite_weight);
}

TEST_CASE("Checked weights reject intermediate integer overflow consistently", "[weights]") {
    const auto weight = GENERATE(std::numeric_limits<I>::min(), std::numeric_limits<I>::max());
    const auto algorithm = GENERATE(Algorithm::prim, Algorithm::kruskal);
    GraphBuilder<I> builder{3};
    REQUIRE(builder.add_edge(V{0}, V{1}, weight));
    REQUIRE(builder.add_edge(V{1}, V{2}, weight));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto tree = make_algorithm<I>(algorithm)->build(*graph);
    REQUIRE_FALSE(tree);
    CHECK(tree.error().code == ErrorCode::weight_overflow);
}

TEST_CASE("Compensated floating totals retain a small term between huge values", "[weights]") {
    GraphBuilder<double> builder{4};
    REQUIRE(builder.add_edge(V{0}, V{1}, -1e100));
    REQUIRE(builder.add_edge(V{1}, V{2}, 1.0));
    REQUIRE(builder.add_edge(V{2}, V{3}, 1e100));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto tree = KruskalAlgorithm<double>{}.build(*graph);
    REQUIRE(tree);
    CHECK(tree->weight() == 1.0L);
    CHECK(MstValidator<double>::validate(*graph, *tree));
}

TEST_CASE("Disconnected input and invalid Prim start return distinct errors", "[mst]") {
    const auto graph = GraphReader<I>::read(std::filesystem::path{MST_EXAMPLES} / "disconnected.txt");
    REQUIRE(graph);
    const auto prim = PrimAlgorithm<I>{}.build(*graph);
    const auto kruskal = KruskalAlgorithm<I>{}.build(*graph);
    const auto start = PrimAlgorithm<I>{}.build(*graph, {.start = V{4}});
    REQUIRE_FALSE(prim);
    REQUIRE_FALSE(kruskal);
    REQUIRE_FALSE(start);
    CHECK(prim.error().code == ErrorCode::disconnected);
    CHECK(kruskal.error().code == ErrorCode::disconnected);
    CHECK(start.error().code == ErrorCode::vertex_out_of_range);
}

TEST_CASE("Exchange validation rejects a connected non-minimal negative tree", "[validation]") {
    GraphBuilder<I> builder{3};
    REQUIRE(builder.add_edge(V{0}, V{1}, -4));
    REQUIRE(builder.add_edge(V{0}, V{2}, -1));
    REQUIRE(builder.add_edge(V{1}, V{2}, -3));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const std::array candidate{E{V{0}, V{1}, -4}, E{V{0}, V{2}, -1}};
    REQUIRE(MstValidator<I>::validate(*graph, 3, candidate, -5, false));
    const auto minimum = MstValidator<I>::validate(*graph, 3, candidate, -5);
    REQUIRE_FALSE(minimum);
    CHECK(minimum.error().code == ErrorCode::nonminimal_tree);
    CHECK(MstValidator<I>::brute_force(*graph, 3).value() == -7);
    CHECK(MstValidator<I>::brute_force(*graph, 1).error().code == ErrorCode::combination_limit);
}

TEST_CASE("A long chain is traversed without recursion and outlives the input graph", "[path]") {
    constexpr std::size_t count = 100'000;
    const auto tree = [] {
        GraphBuilder<I> builder{count};
        for (std::size_t i = 1; i < count; ++i) {
            REQUIRE(builder.add_edge(V{i - 1}, V{i}, -1));
        }
        const auto graph = builder.finish();
        REQUIRE(graph);
        return PrimAlgorithm<I>{}.build(*graph);
    }();
    REQUIRE(tree);
    const auto path = PathFinder<I>::find(*tree, V{count - 1}, V{0});
    REQUIRE(path);
    CHECK(path->vertices.size() == count);
    CHECK(path->edges.size() == count - 1);
    CHECK(path->weight == -99'999);
}

TEST_CASE("PMR tracks requested bytes throughout allocation and release", "[memory]") {
    TrackingMemoryResource memory;
    {
        std::pmr::vector<I> vector{&memory};
        vector.reserve(10);
        CHECK(memory.stats().current_bytes == vector.capacity() * sizeof(I));
        vector.reserve(20);
        CHECK(memory.stats().allocation_count == 2);
        CHECK(memory.stats().deallocation_count == 1);
        CHECK(memory.stats().total_bytes == memory.stats().peak_bytes);
    }
    CHECK(memory.stats().current_bytes == 0);
    CHECK(memory.stats().deallocation_count == 2);
}

TEST_CASE("Brute force ignores unrepresentable weights of heavier candidates", "[validation][weights]") {
    constexpr auto maximum = std::numeric_limits<I>::max();
    GraphBuilder<I> builder{3};
    REQUIRE(builder.add_edge(V{0}, V{1}, 0));
    REQUIRE(builder.add_edge(V{0}, V{2}, maximum));
    REQUIRE(builder.add_edge(V{1}, V{2}, maximum));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto reference = MstValidator<I>::brute_force(*graph);
    REQUIRE(reference);
    CHECK(*reference == maximum);
}

TEST_CASE("Trace recovers a representable subtotal after transient overflow", "[trace][weights]") {
    constexpr auto maximum = std::numeric_limits<I>::max();
    GraphBuilder<I> builder{4};
    REQUIRE(builder.add_edge(V{0}, V{1}, maximum));
    REQUIRE(builder.add_edge(V{1}, V{2}, maximum));
    REQUIRE(builder.add_edge(V{2}, V{3}, -maximum));
    const auto graph = builder.finish();
    REQUIRE(graph);
    std::vector<std::optional<I>> totals;
    const auto trace = [&](const TraceEvent<I>& event) {
        totals.push_back(std::get<PrimStep<I>>(event).common.subtotal);
    };
    const auto tree = PrimAlgorithm<I>{}.build(*graph, {.trace = trace});
    REQUIRE(tree);
    CHECK(tree->weight() == maximum);
    CHECK(totals == std::vector<std::optional<I>>{maximum, std::nullopt, maximum});
    const auto path = PathFinder<I>::find(*tree, V{0}, V{3});
    REQUIRE_FALSE(path);
    CHECK(path.error().code == ErrorCode::weight_overflow);
}

TEST_CASE("Floating serialization round trips exactly and reports failures", "[io]") {
    GraphBuilder<double> builder{3};
    REQUIRE(builder.add_edge(V{1}, V{0}, -0.0));
    REQUIRE(builder.add_edge(V{1}, V{2}, std::nextafter(0.1, 1.0)));
    const auto graph = builder.finish();
    REQUIRE(graph);
    CHECK_FALSE(std::signbit(graph->edge(EdgeId{0}).weight));
    std::ostringstream output;
    REQUIRE(GraphWriter<double>::write(*graph, output));
    std::istringstream input{output.str()};
    const auto parsed = GraphReader<double>::parse(input);
    REQUIRE(parsed);
    const auto reloaded = parsed->finish();
    REQUIRE(reloaded);
    CHECK(std::ranges::equal(graph->edges(), reloaded->edges()));

    input.setstate(std::ios::badbit);
    const auto read_failed = GraphReader<double>::parse(input);
    REQUIRE_FALSE(read_failed);
    CHECK(read_failed.error().code == ErrorCode::read_error);

    output.setstate(std::ios::badbit);
    const auto write_failed = GraphWriter<double>::write(*graph, output);
    REQUIRE_FALSE(write_failed);
    CHECK(write_failed.error().code == ErrorCode::write_error);
}

TEST_CASE("Structural validation checks the candidate independently of the solver", "[validation]") {
    GraphBuilder<I> builder{4};
    REQUIRE(builder.add_edge(V{0}, V{1}, -2));
    REQUIRE(builder.add_edge(V{1}, V{2}, -1));
    REQUIRE(builder.add_edge(V{0}, V{2}, 0));
    REQUIRE(builder.add_edge(V{2}, V{3}, 1));
    const auto graph = builder.finish();
    REQUIRE(graph);
    const auto edges = graph->edges();
    const std::array valid{edges[0], edges[1], edges[3]};
    const std::array cyclic{edges[0], edges[1], edges[2]};
    const std::array repeated{edges[0], edges[1], edges[1]};
    const std::array wrong_weight{edges[0], edges[1], E{V{2}, V{3}, 2}};
    CHECK(MstValidator<I>::validate(*graph, 4, valid, -2, false));
    CHECK_FALSE(MstValidator<I>::validate(*graph, 3, valid, -2, false));
    CHECK_FALSE(MstValidator<I>::validate(*graph, 4, valid, -3, false));
    CHECK_FALSE(MstValidator<I>::validate(*graph, 4, cyclic, -3, false));
    CHECK_FALSE(MstValidator<I>::validate(*graph, 4, repeated, -4, false));
    CHECK_FALSE(MstValidator<I>::validate(*graph, 4, wrong_weight, -1, false));
}
