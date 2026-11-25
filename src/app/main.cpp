#include "mst/algorithms/strategies.hpp"
#include "mst/benchmark/benchmark.hpp"
#include "mst/io/GraphFileReader.hpp"
#include "mst/io/GraphWriter.hpp"
#include "mst/validation/mst_validator.hpp"

#include <cstdio>
#include <iomanip>
#include <iostream>
#include <print>
#include <sstream>

namespace mst {
namespace {

constexpr std::string_view help = R"(Minimal Spanning Tree

Usage
  mst                            Interactive menu
  mst solve GRAPH [--algorithm prim|kruskal] [--start V] [--path FROM TO]
                  [--trace] [--validate] [--weight-type int64|double] [--memory-limit BYTES]
  mst compare GRAPH [--start V] [--weight-type int64|double] [--memory-limit BYTES]
  mst generate --vertices N [--edges M | --density F] --output GRAPH
               [--family chain|star|tree|sparse|dense|complete|equal|signed_weights|disconnected]
               [--seed N] [--engine mt19937|philox] [--min-weight W] [--max-weight W]
               [--weight-type int64|double] [--memory-limit BYTES]
  mst benchmark --config FILE --output FILE.csv
  mst --help
  mst --version

Defaults are Prim, start vertex 1, and int64 weights.
Exit codes are 0 for success, 1 for a data or I/O error, 2 for invalid command usage,
and 3 for an exceptional failure.
)";

enum class Option {
    algorithm,
    start,
    path,
    trace,
    validate,
    weight_type,
    memory_limit,
    vertices,
    edges,
    density,
    output,
    family,
    seed,
    engine,
    min_weight,
    max_weight,
    config,
    count
};

using Values = std::array<std::string_view, 2>;

struct OptionSpec {
    std::string_view name;
    std::size_t argument_count;
};

constexpr std::array option_specs{
    OptionSpec{"--algorithm", 1},    OptionSpec{"--start", 1},    OptionSpec{"--path", 2},
    OptionSpec{"--trace", 0},        OptionSpec{"--validate", 0}, OptionSpec{"--weight-type", 1},
    OptionSpec{"--memory-limit", 1}, OptionSpec{"--vertices", 1}, OptionSpec{"--edges", 1},
    OptionSpec{"--density", 1},      OptionSpec{"--output", 1},   OptionSpec{"--family", 1},
    OptionSpec{"--seed", 1},         OptionSpec{"--engine", 1},   OptionSpec{"--min-weight", 1},
    OptionSpec{"--max-weight", 1},   OptionSpec{"--config", 1}};
static_assert(option_specs.size() == static_cast<std::size_t>(Option::count));

class CliOptions {
  public:
    [[nodiscard]] static Result<CliOptions> parse(std::span<const std::string_view> args,
                                                  std::initializer_list<Option> allowed) {
        CliOptions result;
        while (!args.empty()) {
            const auto entry = std::ranges::find(option_specs, args.front(), &OptionSpec::name);
            if (entry == option_specs.end()) {
                return fail(ErrorCode::invalid_option, std::format("unknown option '{}'", args.front()));
            }
            const auto index = static_cast<std::size_t>(entry - option_specs.begin());
            if (!std::ranges::contains(allowed, static_cast<Option>(index))) {
                return fail(ErrorCode::invalid_option,
                            std::format("{} is not available for this command", entry->name));
            }
            if (result.values_[index]) {
                return fail(ErrorCode::invalid_option, std::format("duplicate option {}", entry->name));
            }
            args = args.subspan(1);
            if (args.size() < entry->argument_count ||
                std::ranges::any_of(args.first(std::min(args.size(), entry->argument_count)),
                                    [](auto value) { return value.starts_with("--"); })) {
                return fail(ErrorCode::invalid_option, std::format("missing argument for {}", entry->name));
            }
            Values values{};
            std::ranges::copy(args.first(entry->argument_count), values.begin());
            result.values_[index] = values;
            args = args.subspan(entry->argument_count);
        }
        return result;
    }

    [[nodiscard]] const std::optional<Values>& operator[](Option option) const {
        return values_[static_cast<std::size_t>(option)];
    }

    [[nodiscard]] std::string_view value(Option option, std::string_view fallback = {}) const {
        return (*this)[option]
            .transform([](const Values& values) { return values.front(); })
            .value_or(fallback);
    }

    template <Number T> [[nodiscard]] Result<T> number(Option option, T fallback) const {
        if (!(*this)[option]) {
            return fallback;
        }
        return parse_option_number<T>(value(option));
    }

    template <Number T> [[nodiscard]] static Result<T> parse_option_number(std::string_view value) {
        auto number = parse_number<T>(value);
        if (!number) {
            number.error().code = ErrorCode::invalid_option;
        }
        return number;
    }

  private:
    std::array<std::optional<Values>, option_specs.size()> values_{};
};

[[nodiscard]] Result<VertexId> user_vertex(std::string_view text) {
    return CliOptions::parse_option_number<std::size_t>(text).and_then(
        [](std::size_t value) -> Result<VertexId> {
            if (value == 0) {
                return fail(ErrorCode::vertex_out_of_range, "vertex numbering starts at 1");
            }
            return VertexId{value - 1};
        });
}

[[nodiscard]] Result<std::pair<VertexId, VertexId>> user_path(std::string_view first,
                                                              std::string_view second) {
    return user_vertex(first).and_then([&](VertexId from) {
        return user_vertex(second).transform([&](VertexId to) { return std::pair{from, to}; });
    });
}

template <typename Action> [[nodiscard]] Result<void> with_weight(WeightKind kind, Action action) {
    if (kind == WeightKind::int64) {
        return action.template operator()<std::int64_t>();
    }
    return action.template operator()<double>();
}

template <WeightType W> void print_tree(std::string_view algorithm, const SpanningTree<W>& tree) {
    std::println("{}: {} edges", algorithm, tree.edges().size());
    for (const auto& edge : tree.edges()) {
        std::println("{}-{} ({})", edge.u.index() + 1, edge.v.index() + 1, edge.weight);
    }
    std::println("MST weight: {}", tree.weight());
}

template <WeightType W> void print_trace(const TraceEvent<W>& event) {
    detail::visit(
        [](const auto& step) {
            const auto& [edge, decision, subtotal, accepted] = step.common;
            const auto reason = decision == Decision::accepted ? "accepted (joins two parts)"
                                : decision == Decision::cycle  ? "rejected (cycle)"
                                                               : "rejected (both endpoints included)";
            std::print("{}-{} ({}): {}; subtotal {}; selected {}; ", edge.u.index() + 1, edge.v.index() + 1,
                       edge.weight, reason, subtotal ? number_text(*subtotal) : "overflow", accepted);
            if constexpr (requires { step.included_vertices; }) {
                std::println("included vertices {}", step.included_vertices);
            } else {
                std::println("components {}", step.components);
            }
        },
        event);
}

template <WeightType W>
[[nodiscard]] Result<SpanningTree<W>> solve(const Graph<W>& graph, Algorithm algorithm, VertexId start,
                                            bool trace) {
    const auto strategy = make_algorithm<W>(algorithm);
    BuildOptions<W> options{.start = start};
    const auto callback = [](const TraceEvent<W>& event) { print_trace<W>(event); };
    if (trace) {
        options.trace = callback;
    }
    auto tree = strategy->build(graph, options);
    if (tree) {
        print_tree(strategy->name(), *tree);
    }
    return tree;
}

template <WeightType W>
[[nodiscard]] Result<void> show_path(const SpanningTree<W>& tree, std::pair<VertexId, VertexId> endpoints) {
    return PathFinder<W>::find(tree, endpoints.first, endpoints.second)
        .transform([](const PathResult<W>& path) {
            for (const auto [index, vertex] : path.vertices | std::views::enumerate) {
                std::print("{}{}", index == 0 ? "" : " -> ", vertex.index() + 1);
            }
            std::println("\nPath weight: {}", path.weight);
        });
}

template <WeightType W>
[[nodiscard]] Result<void> validate(const Graph<W>& graph, const SpanningTree<W>& tree) {
    return MstValidator<W>::validate(graph, tree).transform([] { std::println("MST is valid and minimum"); });
}

template <WeightType W> [[nodiscard]] Result<void> compare(const Graph<W>& graph, VertexId start = {}) {
    const auto prim = solve(graph, Algorithm::prim, start, false);
    if (!prim) {
        return std::unexpected{prim.error()};
    }
    const auto kruskal = solve(graph, Algorithm::kruskal, {}, false);
    if (!kruskal) {
        return std::unexpected{kruskal.error()};
    }
    return MstValidator<W>::validate(graph, *prim)
        .and_then([&] { return MstValidator<W>::validate(graph, *kruskal); })
        .and_then([&]() -> Result<void> {
            if (!WeightTraits<W>::equivalent(prim->weight(), kruskal->weight())) {
                return fail(ErrorCode::invalid_tree, "MST weights disagree");
            }
            std::println("Both MSTs are valid and minimum. Weight: {}", prim->weight());
            return {};
        });
}

[[nodiscard]] Result<Algorithm> read_algorithm(std::string_view name) {
#if MST_HAS_REFLECTION
    constexpr auto names = detail::enum_names<Algorithm>();
#else
    using namespace std::string_view_literals;
    constexpr std::array names{"prim"sv, "kruskal"sv};
#endif
    return choice<Algorithm>(name, names);
}

[[nodiscard]] Result<void> solve_cli(std::span<const std::string_view> args, bool comparison) {
    if (args.empty() || args.front().empty() || args.front().starts_with("--")) {
        return fail(ErrorCode::invalid_option, "expected an input graph path");
    }
    const auto parsed =
        comparison
            ? CliOptions::parse(args.subspan(1), {Option::start, Option::weight_type, Option::memory_limit})
            : CliOptions::parse(args.subspan(1),
                                {Option::algorithm, Option::start, Option::path, Option::trace,
                                 Option::validate, Option::weight_type, Option::memory_limit});
    if (!parsed) {
        return std::unexpected{parsed.error()};
    }
    const auto weight = parse_weight_kind(parsed->value(Option::weight_type, "int64"));
    const auto algorithm = read_algorithm(parsed->value(Option::algorithm, "prim"));
    const auto start = user_vertex(parsed->value(Option::start, "1"));
    const auto memory = parsed->number(Option::memory_limit, default_memory_limit);
    if (!weight || !algorithm || !start || !memory) {
        return std::unexpected{!weight      ? weight.error()
                               : !algorithm ? algorithm.error()
                               : !start     ? start.error()
                                            : memory.error()};
    }
    if (*algorithm == Algorithm::kruskal && (*parsed)[Option::start]) {
        return fail(ErrorCode::invalid_option, "--start is only used by Prim");
    }
    std::optional<std::pair<VertexId, VertexId>> path;
    if (const auto& value = (*parsed)[Option::path]) {
        const auto endpoints = user_path((*value)[0], (*value)[1]);
        if (!endpoints) {
            return std::unexpected{endpoints.error()};
        }
        path.emplace(*endpoints);
    }
    return with_weight(*weight, [&]<WeightType W>() -> Result<void> {
        const auto graph = GraphReader<W>::read(std::filesystem::path{args.front()}, *memory);
        if (!graph) {
            return std::unexpected{graph.error()};
        }
        if (comparison) {
            return compare(*graph, *start);
        }
        const auto tree = solve(*graph, *algorithm, *start, (*parsed)[Option::trace].has_value());
        if (!tree) {
            return std::unexpected{tree.error()};
        }
        if ((*parsed)[Option::validate]) {
            if (const auto checked = validate(*graph, *tree); !checked) {
                return checked;
            }
        }
        return path ? show_path(*tree, *path) : Result<void>{};
    });
}

template <WeightType W> [[nodiscard]] Result<void> generate(const CliOptions& args) {
    const auto vertices = args.number<std::size_t>(Option::vertices, 0);
    const auto seed = args.number<std::uint64_t>(Option::seed, 42);
    const auto memory = args.number(Option::memory_limit, default_memory_limit);
    const auto family = choice<GraphFamily>(args.value(Option::family, "sparse"), graph_family_names());
    const auto engine = choice<RandomEngine>(args.value(Option::engine, "mt19937"), random_engine_names());
    const auto low = args.number<W>(Option::min_weight, -100);
    const auto high = args.number<W>(Option::max_weight, 100);
    if (!vertices || !seed || !memory || !family || !engine || !low || !high) {
        return fail(ErrorCode::invalid_option,
                    "invalid vertex count, seed, memory limit, family, engine or weight");
    }
    GenerationOptions<W> settings{.vertices = *vertices,
                                  .low = *low,
                                  .high = *high,
                                  .seed = *seed,
                                  .memory_limit_bytes = *memory,
                                  .family = *family,
                                  .engine = *engine};
    if (args[Option::edges]) {
        const auto edges = CliOptions::parse_option_number<std::size_t>(args.value(Option::edges));
        if (!edges) {
            return std::unexpected{edges.error()};
        }
        settings.edges = *edges;
    }
    if (args[Option::density]) {
        const auto density = CliOptions::parse_option_number<double>(args.value(Option::density));
        if (!density) {
            return std::unexpected{density.error()};
        }
        settings.density = *density;
    }
    auto graph =
        GraphGenerator<W>::generate(settings).and_then([](const auto& draft) { return draft.finish(); });
    if (!graph) {
        return std::unexpected{std::move(graph.error())};
    }
    const std::filesystem::path output{args.value(Option::output)};
    return GraphWriter<W>::write(*graph, output).transform([&] {
        std::println("Generated {} vertices and {} edges in {}", graph->vertex_count(), graph->edges().size(),
                     output.string());
    });
}

[[nodiscard]] Result<void> generate_cli(std::span<const std::string_view> args) {
    const auto options =
        CliOptions::parse(args, {Option::vertices, Option::edges, Option::density, Option::seed,
                                 Option::output, Option::family, Option::engine, Option::weight_type,
                                 Option::min_weight, Option::max_weight, Option::memory_limit});
    if (!options) {
        return std::unexpected{options.error()};
    }
    if (!(*options)[Option::vertices] || options->value(Option::output).empty()) {
        return fail(ErrorCode::invalid_option, "generate requires --vertices and --output");
    }
    return parse_weight_kind(options->value(Option::weight_type, "int64")).and_then([&](WeightKind kind) {
        return with_weight(kind, [&]<WeightType W> { return generate<W>(*options); });
    });
}

[[nodiscard]] Result<void> benchmark(const std::filesystem::path& config,
                                     const std::filesystem::path& output) {
    return BenchmarkRunner::read_config(config)
        .and_then([&](BenchmarkConfig&& parsed) { return BenchmarkRunner{std::move(parsed)}.run(output); })
        .transform([&] { std::println("Benchmark results written to {}", output.string()); });
}

[[nodiscard]] Result<void> benchmark_cli(std::span<const std::string_view> args) {
    const auto options = CliOptions::parse(args, {Option::config, Option::output});
    if (!options) {
        return std::unexpected{options.error()};
    }
    const auto config = options->value(Option::config);
    const auto output = options->value(Option::output);
    if (config.empty() || output.empty()) {
        return fail(ErrorCode::invalid_option, "benchmark requires --config and --output");
    }
    return benchmark(std::filesystem::path{config}, std::filesystem::path{output});
}

[[nodiscard]] std::optional<std::string> prompt(std::string_view label) {
    std::print("{}", label);
    std::fflush(stdout);
    std::string value;
    if (!std::getline(std::cin, value)) {
        return std::nullopt;
    }
    return value;
}

void print_error(const Error& error) { std::println(stderr, "Error: {}", describe(error)); }

template <WeightType W> struct Session {
    Graph<W> graph;
    std::optional<SpanningTree<W>> tree{};
};

class Application {
  public:
    static int run(std::span<const std::string_view> args) {
        if (args.empty()) {
            return Application{}.interactive();
        }
        if (args.size() == 1 && args.front() == "--help") {
            std::print("{}", help);
            return 0;
        }
        if (args.size() == 1 && args.front() == "--version") {
            std::println("mst {} | {} | {} | {}", MST_PROJECT_VERSION, MST_PROFILE, MST_COMPILER,
                         MST_BUILD_TYPE);
#if MST_HAS_REFLECTION
            std::println("{} and {}", strategy_type<std::int64_t>(Algorithm::prim),
                         strategy_type<std::int64_t>(Algorithm::kruskal));
#endif
            return 0;
        }
        const auto result = dispatch(args.front(), args.subspan(1));
        if (!result) {
            print_error(result.error());
            return result.error().code == ErrorCode::invalid_option ? 2 : 1;
        }
        return 0;
    }

  private:
    static Result<void> dispatch(std::string_view command, std::span<const std::string_view> args) {
        if (command == "solve" || command == "compare") {
            return solve_cli(args, command == "compare");
        }
        if (command == "generate") {
            return generate_cli(args);
        }
        if (command == "benchmark") {
            return benchmark_cli(args);
        }
        return fail(ErrorCode::invalid_option, std::format("unknown command '{}'; see mst --help", command));
    }

    int interactive() {
        constexpr std::string_view menu = R"(
Minimal Spanning Tree
1. Load graph from file
2. Display graph
3. Build MST using Prim
4. Build MST using Kruskal
5. Compare algorithms
6. Find path in MST
7. Validate MST
8. Run benchmarks
9. Generate graph
0. Exit
> )";
        while (const auto selection = prompt(menu)) {
            if (*selection == "0") {
                return 0;
            }
            if (const auto result = interact(*selection); !result) {
                print_error(result.error());
            }
        }
        return 0;
    }

    Result<void> interact(std::string_view selection) {
        if (selection == "1") {
            return load();
        }
        if (selection == "8") {
            const auto config = prompt("Benchmark config path: ");
            if (!config) {
                return {};
            }
            const auto output = prompt("Output CSV path: ");
            return output ? benchmark(*config, *output) : Result<void>{};
        }
        if (selection == "9") {
            std::println("Use the options from 'mst --help'. Quoting supports spaces in paths.");
            const auto line = prompt("Generate options: ");
            if (!line) {
                return {};
            }
            std::istringstream input{*line};
            std::vector<std::string> words;
            for (std::string word; input >> std::quoted(word);) {
                words.push_back(std::move(word));
            }
            if (input.fail() && !input.eof()) {
                return fail(ErrorCode::invalid_option, "invalid quoted option");
            }
            std::vector<std::string_view> views(words.begin(), words.end());
            return generate_cli(views);
        }
        constexpr std::array<std::string_view, 6> actions{"2", "3", "4", "5", "6", "7"};
#if MST_HAS_RANGES_DEFAULT_VALUE
        if (!std::ranges::contains(actions, {selection})) {
#else
        if (!std::ranges::contains(actions, selection)) {
#endif
            return fail(ErrorCode::invalid_option, "choose a menu item from 0 to 9");
        }
        return detail::visit([&](auto& session) { return interact_with_graph(session, selection); },
                             session_);
    }

    Result<void> load() {
        const auto path = prompt("Graph file path: ");
        if (!path) {
            return {};
        }
        const auto kind = prompt("Weight type [int64]: ");
        if (!kind) {
            return {};
        }
        return parse_weight_kind(kind->empty() ? "int64" : *kind).and_then([&](WeightKind weight) {
            return with_weight(weight, [&]<WeightType W> {
                return GraphReader<W>::read(std::filesystem::path{*path}).transform([&](Graph<W>&& graph) {
                    std::println("Loaded {} vertices and {} edges", graph.vertex_count(),
                                 graph.edges().size());
                    session_.emplace<Session<W>>(std::move(graph));
                });
            });
        });
    }

    static Result<void> interact_with_graph(std::monostate, std::string_view) {
        std::println("Load a graph first");
        return {};
    }

    template <WeightType W>
    static Result<void> interact_with_graph(Session<W>& session, std::string_view selection) {
        const auto& graph = session.graph;
        if (selection == "2") {
            return GraphWriter<W>::write(graph, std::cout);
        }
        if (selection == "5") {
            return compare(graph);
        }
        if (selection == "3" || selection == "4") {
            VertexId start{};
            bool trace = false;
            if (selection == "3") {
                const auto request = prompt("Start vertex [1] and optional --trace: ");
                if (!request) {
                    return {};
                }
                std::istringstream input{*request};
                std::string vertex, option, extra;
                if (input >> vertex) {
                    if (vertex == "--trace") {
                        trace = true;
                    } else {
                        const auto parsed = user_vertex(vertex);
                        if (!parsed) {
                            return std::unexpected{parsed.error()};
                        }
                        start = *parsed;
                    }
                    if (input >> option) {
                        if (trace || option != "--trace" || (input >> extra)) {
                            return fail(ErrorCode::invalid_option,
                                        "expected a vertex followed by optional --trace");
                        }
                        trace = true;
                    }
                }
            }
            return solve(graph, selection == "3" ? Algorithm::prim : Algorithm::kruskal, start, trace)
                .transform([&](SpanningTree<W>&& tree) { session.tree.emplace(std::move(tree)); });
        }
        if (!session.tree) {
            std::println("Build an MST first");
            return {};
        }
        if (selection == "7") {
            return validate(graph, *session.tree);
        }
        const auto line = prompt("Path endpoints: ");
        if (!line) {
            return {};
        }
        std::istringstream input{*line};
        std::string first, second, extra;
        if (!(input >> first >> second) || (input >> extra)) {
            return fail(ErrorCode::invalid_option, "enter exactly two vertex numbers");
        }
        return user_path(first, second).and_then([&](auto endpoints) {
            return show_path(*session.tree, endpoints);
        });
    }

    std::variant<std::monostate, Session<std::int64_t>, Session<double>> session_;
};

} // namespace
} // namespace mst

int main(int argc, char* argv[]) try {
    const auto args = std::span{argv, static_cast<std::size_t>(argc)}.subspan(argc > 0 ? 1 : 0);
    std::vector<std::string_view> views(args.begin(), args.end());
    return mst::Application::run(views);
} catch (const std::exception& error) {
    std::fprintf(stderr, "Fatal error: %s\n", error.what());
    return 3;
}
