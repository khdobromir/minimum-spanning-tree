#include "mst/benchmark/benchmark.hpp"

#include "mst/algorithms/strategies.hpp"
#include "mst/io/GraphFileReader.hpp"
#include "mst/io/atomic_output.hpp"
#include "mst/utility/tracking_memory.hpp"
#include "mst/validation/mst_validator.hpp"

#include <chrono>
#include <format>
#include <iomanip>
#include <sstream>
#include <sys/utsname.h>

namespace mst {
namespace {

template <WeightType W>
Result<Experiment> generation_case(GraphFamily family, std::size_t vertices, std::string_view amount,
                                   std::uint64_t seed, std::string_view low, std::string_view high,
                                   RandomEngine engine) {
    GenerationOptions<W> result{.vertices = vertices, .seed = seed, .family = family, .engine = engine};
    if (amount.starts_with("density=")) {
        const auto density = parse_number<double>(amount.substr(8));
        if (!density) {
            return std::unexpected{density.error()};
        }
        result.density = *density;
    } else if (amount != "auto") {
        const auto edges = parse_number<std::size_t>(amount);
        if (!edges) {
            return std::unexpected{edges.error()};
        }
        result.edges = *edges;
    }
    const auto minimum = parse_number<W>(low);
    const auto maximum = parse_number<W>(high);
    if (!minimum || !maximum) {
        return std::unexpected{minimum ? maximum.error() : minimum.error()};
    }
    result.low = *minimum;
    result.high = *maximum;
    return Experiment{result};
}

Result<Experiment> parse_generation_case(std::istream& input) {
    std::string family_token, n_token, amount, weight_type, seed_token, low, high, engine_token;
    if (!(input >> family_token >> n_token >> amount >> weight_type >> seed_token >> low >> high >>
          engine_token)) {
        return fail(ErrorCode::invalid_config,
                    "expected family n m|density=F|auto type seed low high engine");
    }
    const auto family = choice<GraphFamily>(family_token, graph_family_names());
    const auto vertices = parse_number<std::size_t>(n_token);
    const auto seed = parse_number<std::uint64_t>(seed_token);
    const auto weight = parse_weight_kind(weight_type);
    const auto engine = choice<RandomEngine>(engine_token, random_engine_names());
    if (!family || !vertices || !seed || !weight || !engine) {
        return fail(ErrorCode::invalid_config,
                    "invalid graph family, vertex count, seed, weight type or engine");
    }
    if (*weight == WeightKind::int64) {
        return generation_case<std::int64_t>(*family, *vertices, amount, *seed, low, high, *engine);
    }
    return generation_case<double>(*family, *vertices, amount, *seed, low, high, *engine);
}

Result<Experiment> parse_file_case(std::istream& input, const std::filesystem::path& config_dir) {
    std::string file, type;
    if (!(input >> std::quoted(file) >> type) || file.empty()) {
        return fail(ErrorCode::invalid_config, "expected graph file path and weight type");
    }
    return parse_weight_kind(type).transform([&](WeightKind kind) -> Experiment {
        auto path = config_dir / file;
        if (kind == WeightKind::int64) {
            return FileExperiment<std::int64_t>{std::move(path)};
        }
        return FileExperiment<double>{std::move(path)};
    });
}

Result<void> apply_config(BenchmarkConfig& config, std::string_view name, std::istream& input,
                          const std::filesystem::path& directory) {
    if (name == "case" || name == "file") {
        auto experiment = name == "case" ? parse_generation_case(input) : parse_file_case(input, directory);
        if (!experiment) {
            return std::unexpected{std::move(experiment.error())};
        }
        config.cases.push_back(std::move(*experiment));
        return {};
    }
    if (name == "path") {
        std::string a, b;
        if (!(input >> a >> b)) {
            return fail(ErrorCode::invalid_config, "expected two vertex numbers for path");
        }
        const auto from = parse_number<std::size_t>(a);
        const auto to = parse_number<std::size_t>(b);
        if (!from || !to || *from == 0 || *to == 0) {
            return fail(ErrorCode::invalid_config, "path vertices must be positive integers");
        }
        config.path.emplace(VertexId{*from - 1}, VertexId{*to - 1});
        return {};
    }
    if (name == "start") {
        std::string value;
        input >> value;
        const auto vertex = parse_number<std::size_t>(value);
        if (!vertex || *vertex == 0) {
            return fail(ErrorCode::invalid_config, "start must be a positive vertex number");
        }
        config.start = VertexId{*vertex - 1};
        return {};
    }
    std::size_t* setting = nullptr;
    if (name == "repetitions") {
        setting = &config.repetitions;
    } else if (name == "warmups") {
        setting = &config.warmups;
    } else if (name == "memory_limit_bytes") {
        setting = &config.memory_limit_bytes;
    } else if (name == "exchange_limit") {
        setting = &config.exchange_limit;
    } else {
        return fail(ErrorCode::invalid_config, "unknown directive '" + std::string{name} + "'");
    }
    std::string token;
    input >> token;
    return parse_number<std::size_t>(token).transform([&](std::size_t number) { *setting = number; });
}

using Time = std::chrono::nanoseconds::rep;

struct Phase {
    std::string_view name;
    std::vector<Time> times{};
    std::optional<MemoryStats> memory{};
    std::optional<MemoryStats> scratch{};
    std::optional<MemoryStats> storage{};
    std::optional<std::size_t> combined_peak{};
    std::string weight{};
    std::string_view validation{"not_applicable"};
};

template <typename Work> auto measure(Phase& phase, bool record, Work&& work) {
    const auto start = std::chrono::steady_clock::now();
    auto value = std::forward<Work>(work)();
    const auto end = std::chrono::steady_clock::now();
    if (record) {
        phase.times.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    }
    return value;
}

struct CaseInfo {
    std::size_t id;
    std::size_t vertices;
    std::size_t edges;
    std::string_view family;
    std::string_view weight_type;
    std::string seed;
    std::string_view engine;
    std::string source;
};

struct Records {
    std::string aggregate;
    std::string raw;
};

std::string csv_field(std::string_view value) {
    if (value.find_first_of(",\"\r\n") == std::string_view::npos) {
        return std::string{value};
    }
    std::string quoted{"\""};
    for (const char ch : value) {
        if (ch == '"') {
            quoted += '"';
        }
        quoted += ch;
    }
    quoted += '"';
    return quoted;
}

void add_phase(Records& records, const CaseInfo& info, const Phase& phase) {
    if (phase.times.empty()) {
        return;
    }
    const auto possible = simple_edge_limit(info.vertices);
    const auto density =
        possible == 0 ? 0.0 : static_cast<double>(info.edges) / static_cast<double>(possible);
    const auto prefix =
        std::format("{},{},{},{},{},{},{},{},{},{}", phase.name, info.family, info.vertices, info.edges,
                    density, info.weight_type, info.seed, info.id, info.engine, csv_field(info.source));
    for (const auto [index, time] : phase.times | std::views::enumerate) {
        records.raw += std::format("{},{},{}\n", prefix, index + 1, time);
    }
    auto times = phase.times;
    std::ranges::sort(times);
    const auto middle = times.size() / 2;
    const auto median = times.size() % 2 ? times[middle] : std::midpoint(times[middle - 1], times[middle]);
    records.aggregate +=
        std::format("{},{},{},{},{},", prefix, times.size(), times.front(), median, times.back());
    if (phase.memory) {
        records.aggregate += std::format("{},{}", phase.memory->peak_bytes, phase.memory->allocation_count);
    } else {
        records.aggregate += ',';
    }
    records.aggregate += std::format(",{},{},", phase.weight, phase.validation);
    if (phase.memory) {
        records.aggregate +=
            std::format("{},{}", phase.memory->total_bytes, phase.memory->deallocation_count);
    } else {
        records.aggregate += ',';
    }
    records.aggregate += ',';
    if (phase.scratch) {
        records.aggregate += number_text(phase.scratch->peak_bytes);
    }
    records.aggregate += ',';
    if (phase.storage) {
        records.aggregate += number_text(phase.storage->peak_bytes);
    }
    records.aggregate += ',';
    if (phase.combined_peak) {
        records.aggregate += number_text(*phase.combined_peak);
    }
    records.aggregate += '\n';
}

template <WeightType W>
Result<std::optional<typename WeightTraits<W>::Sum>>
measure_memory(const IMstAlgorithm<W>& algorithm, const Graph<W>& graph, std::size_t graph_live,
               std::size_t graph_peak, VertexId start, bool connected, bool exchange, Phase& phase) {
    using Sum = WeightTraits<W>::Sum;
    TrackingMemoryResource combined;
    TrackingMemoryResource scratch{&combined};
    TrackingMemoryResource storage{&combined};
    std::optional<Sum> sum;
    {
        const auto tree = algorithm.build(graph, {.start = start, .scratch = &scratch, .storage = &storage});
        if (!tree && tree.error().code != ErrorCode::disconnected) {
            return std::unexpected{tree.error()};
        }
        if (static_cast<bool>(tree) != connected) {
            return fail(ErrorCode::invalid_tree, "algorithm disagrees with graph connectivity");
        }
        if (tree) {
            if (const auto valid = MstValidator<W>::validate(graph, *tree, exchange); !valid) {
                return std::unexpected{valid.error()};
            }
            sum = tree->weight();
            phase.weight = number_text(*sum);
            phase.validation = exchange ? "valid_exchange" : "valid_structure_only";
        } else {
            phase.validation = "disconnected";
        }
        MST_ASSERT(scratch.stats().current_bytes == 0);
    }
    phase.memory = combined.stats();
    phase.scratch = scratch.stats();
    phase.storage = storage.stats();
    phase.combined_peak = std::max(graph_peak, graph_live + combined.stats().peak_bytes);
    return sum;
}

template <WeightType W> bool is_connected(const Graph<W>& graph) {
    DisjointSetUnion sets{graph.vertex_count()};
    for (const auto& edge : graph.edges()) {
        static_cast<void>(sets.unite(edge.u, edge.v));
    }
    return sets.component_count() == 1;
}

template <typename Options>
Result<void> run_case(const Options& options, const BenchmarkConfig& config, std::size_t id,
                      Records& records) {
    using W = Options::Weight;
    constexpr bool from_file = std::same_as<Options, FileExperiment<W>>;
    CaseInfo info{.id = id,
                  .vertices = 0,
                  .edges = 0,
                  .family = "file",
                  .weight_type = weight_kind_name<W>(),
                  .seed = {},
                  .engine = {},
                  .source = {}};
    Phase read{.name = "read"};
    Phase prepare{.name = "prepare"};
    Phase prim{.name = "prim"};
    Phase kruskal{.name = "kruskal"};
    Phase path_prim{.name = "path_prim"};
    Phase path_kruskal{.name = "path_kruskal"};
    const auto count = config.repetitions + config.warmups;

    TrackingMemoryResource graph_memory;
    std::optional<Graph<W>> graph;
    std::optional<Graph<W>> tracked_graph;
    {
        std::optional<GraphBuilder<W>> draft;
        if constexpr (from_file) {
            info.source = options.path.string();
            for (std::size_t index = 0; index < count; ++index) {
                draft.reset();
                auto parsed = measure(read, index >= config.warmups, [&] {
                    return GraphReader<W>::read_draft(options.path, config.memory_limit_bytes);
                });
                if (!parsed) {
                    return std::unexpected{std::move(parsed.error())};
                }
                draft.emplace(std::move(*parsed));
            }
        } else {
            info.family = name(options.family);
            info.seed = number_text(options.seed);
            info.engine = name(options.engine);
            auto parameters = options;
            parameters.memory_limit_bytes = config.memory_limit_bytes;
            auto generated = GraphGenerator<W>::generate(parameters);
            if (!generated) {
                return std::unexpected{std::move(generated.error())};
            }
            draft.emplace(std::move(*generated));
        }
        info.vertices = draft->vertex_count();
        info.edges = draft->edge_count();
        for (std::size_t index = 0; index < count; ++index) {
            graph.reset();
            auto prepared = measure(prepare, index >= config.warmups, [&] { return draft->finish(); });
            if (!prepared) {
                return std::unexpected{std::move(prepared.error())};
            }
            graph.emplace(std::move(*prepared));
        }
        auto prepared = draft->finish(&graph_memory);
        if (!prepared) {
            return std::unexpected{std::move(prepared.error())};
        }
        tracked_graph.emplace(std::move(*prepared));
    }

    const auto [from, to] = config.path.value_or(std::pair{VertexId{0}, VertexId{info.vertices - 1}});
    if (from.index() >= info.vertices || to.index() >= info.vertices) {
        return fail(ErrorCode::vertex_out_of_range, "benchmark path is outside 1..n");
    }
    if (config.start.index() >= info.vertices) {
        return fail(ErrorCode::vertex_out_of_range, "benchmark Prim start is outside 1..n");
    }
    const auto prim_solver = make_algorithm<W>(Algorithm::prim);
    const auto kruskal_solver = make_algorithm<W>(Algorithm::kruskal);
    const BuildOptions<W> build_options{.start = config.start};
    for (std::size_t index = 0; index < count; ++index) {
        const bool record = index >= config.warmups;
        const bool prim_first = index % 2 == 0;
        const auto first = measure(prim_first ? prim : kruskal, record, [&] {
            return (prim_first ? prim_solver : kruskal_solver)->build(*graph, build_options);
        });
        const auto second = measure(prim_first ? kruskal : prim, record, [&] {
            return (prim_first ? kruskal_solver : prim_solver)->build(*graph, build_options);
        });
        if (!first || !second) {
            if (first || first.error().code != ErrorCode::disconnected) {
                return first ? fail(ErrorCode::invalid_tree, "algorithms disagree on connectivity")
                             : std::unexpected{first.error()};
            }
            if (second || second.error().code != ErrorCode::disconnected) {
                return second ? fail(ErrorCode::invalid_tree, "algorithms disagree on connectivity")
                              : std::unexpected{second.error()};
            }
            continue;
        }
        const auto path_first = measure(prim_first ? path_prim : path_kruskal, record,
                                        [&] { return PathFinder<W>::find(*first, from, to); });
        const auto path_second = measure(prim_first ? path_kruskal : path_prim, record,
                                         [&] { return PathFinder<W>::find(*second, from, to); });
        if (!path_first || !path_second) {
            return std::unexpected{path_first ? path_second.error() : path_first.error()};
        }
    }
    graph.reset();

    const bool connected = is_connected(*tracked_graph);
    const bool exchange = info.edges <= config.exchange_limit / info.vertices;
    const auto graph_stats = graph_memory.stats();
    const auto p = measure_memory(*prim_solver, *tracked_graph, graph_stats.current_bytes,
                                  graph_stats.peak_bytes, config.start, connected, exchange, prim);
    if (!p) {
        return std::unexpected{p.error()};
    }
    const auto k = measure_memory(*kruskal_solver, *tracked_graph, graph_stats.current_bytes,
                                  graph_stats.peak_bytes, config.start, connected, exchange, kruskal);
    if (!k) {
        return std::unexpected{k.error()};
    }
    if (*p && *k && !WeightTraits<W>::equivalent(**p, **k)) {
        return fail(ErrorCode::invalid_tree, "algorithms report different MST weights");
    }

    tracked_graph.reset();
    prepare.memory = graph_memory.stats();
    for (const auto* phase : {&read, &prepare, &prim, &kruskal, &path_prim, &path_kruskal}) {
        add_phase(records, info, *phase);
    }
    return {};
}

std::string system_field(const std::filesystem::path& path, std::string_view key, char delimiter) {
    std::ifstream input{path};
    for (std::string line; std::getline(input, line);) {
        const auto separator = line.find(delimiter);
        if (separator == std::string::npos) {
            continue;
        }
        const std::string_view name{line.data(), separator};
        const auto key_end = name.find_last_not_of(detail::whitespace);
        if (key_end == std::string_view::npos || name.substr(0, key_end + 1) != key) {
            continue;
        }
        const auto begin = line.find_first_not_of(" \t\"", separator + 1);
        if (begin != std::string::npos) {
            const auto end = line.find_last_not_of(" \t\"\r");
            return line.substr(begin, end - begin + 1);
        }
    }
    return "unavailable";
}

std::string metadata(const BenchmarkConfig& config) {
    utsname system{};
    const auto kernel = uname(&system) == 0
                            ? std::format("{} {} {}", system.sysname, system.release, system.machine)
                            : "unavailable";
    std::string output{"key,value\n"};
    const auto add = [&](std::string_view key, std::string_view value) {
        output += std::format("{},{}\n", key, csv_field(value));
    };
    add("cpu_model", system_field("/proc/cpuinfo", "model name", ':'));
    add("memory_total", system_field("/proc/meminfo", "MemTotal", ':'));
    add("os", system_field("/etc/os-release", "PRETTY_NAME", '='));
    add("kernel", kernel);
    add("compiler", MST_COMPILER);
    add("build_type", MST_BUILD_TYPE);
    add("compiler_flags", MST_BUILD_FLAGS);
    add("project_version", MST_PROJECT_VERSION);
    add("cpp26_profile", MST_PROFILE);
    add("repetitions", number_text(config.repetitions));
    add("warmups", number_text(config.warmups));
    add("memory_limit_bytes", number_text(config.memory_limit_bytes));
    add("exchange_limit", number_text(config.exchange_limit));
    add("start", number_text(config.start.index() + 1));
    add("path", config.path
                    ? std::format("{} {}", config.path->first.index() + 1, config.path->second.index() + 1)
                    : "1 n");
    return output;
}

Result<void> save(const std::filesystem::path& path, std::string_view content) {
    return detail::write_file(path, [&](std::ostream& stream) -> Result<void> {
        stream << content;
        return {};
    });
}

std::filesystem::path sidecar(const std::filesystem::path& output, std::string_view suffix) {
    return output.parent_path() / (output.stem().string() + std::string{suffix});
}

} // namespace

Result<BenchmarkConfig> BenchmarkRunner::read_config(const std::filesystem::path& path) {
    std::ifstream file{path};
    if (!file) {
        return fail(ErrorCode::file_unavailable, path.string());
    }
    BenchmarkConfig config;
    config.source = path;
    std::size_t line = 0;
    for (std::string row; std::getline(file, row);) {
        ++line;
        std::istringstream input{row};
        std::string directive, excess;
        if (!(input >> directive) || directive.starts_with('#')) {
            continue;
        }
        auto result = apply_config(config, directive, input, path.parent_path());
        if (!result) {
            return fail(ErrorCode::invalid_config, path.string() + ": " + describe(result.error()), line, 1);
        }
        if (input >> excess) {
            return fail(ErrorCode::extra_data, path.string() + ": extra configuration field", line, 1);
        }
    }
    if (file.bad() || !file.eof()) {
        return fail(ErrorCode::read_error, path.string(), line + 1, 1);
    }
    return config;
}

Result<void> BenchmarkRunner::run(const std::filesystem::path& output) const {
    if (config_.cases.empty() || config_.repetitions == 0 || config_.memory_limit_bytes == 0 ||
        config_.repetitions > std::numeric_limits<std::size_t>::max() - config_.warmups) {
        return fail(ErrorCode::invalid_config,
                    "need cases, positive repetitions and memory limit, and a valid warmup count");
    }
    const std::array outputs{output, sidecar(output, ".raw.csv"), sidecar(output, ".metadata.csv")};
    const auto check_overlap = [&](const std::filesystem::path& input) -> Result<void> {
        if (input.empty()) {
            return {};
        }
        for (const auto& target : outputs) {
            std::error_code error;
            if (std::filesystem::equivalent(input, target, error)) {
                return fail(ErrorCode::write_error, target.string() + " overlaps input " + input.string());
            }
        }
        return {};
    };
    if (auto checked = check_overlap(config_.source); !checked) {
        return checked;
    }
    for (const auto& experiment : config_.cases) {
        auto checked = detail::visit(
            [&](const auto& options) -> Result<void> {
                if constexpr (requires { options.path; }) {
                    return check_overlap(options.path);
                }
                return {};
            },
            experiment);
        if (!checked) {
            return checked;
        }
    }
    constexpr std::string_view fields =
        "algorithm,graph_type,vertex_count,edge_count,density,weight_type,seed,case_id,engine,source";
    Records records{
        .aggregate = std::format(
            "{},repetitions,min_time_ns,median_time_ns,max_time_ns,peak_memory_bytes,allocation_count,"
            "mst_weight,validation_status,total_allocation_bytes,deallocation_count,scratch_peak_bytes,"
            "result_peak_bytes,combined_peak_bytes\n",
            fields),
        .raw = std::format("{},repetition,time_ns\n", fields)};
    for (const auto [index, experiment] : config_.cases | std::views::enumerate) {
        const auto id = static_cast<std::size_t>(index) + 1;
        auto result = detail::visit(
            [&](const auto& options) { return run_case(options, config_, id, records); }, experiment);
        if (!result) {
            result.error().detail = std::format("case {}: {}", id, result.error().detail);
            return result;
        }
    }
    return save(outputs[1], records.raw)
        .and_then([&] { return save(outputs[2], metadata(config_)); })
        .and_then([&] { return save(outputs[0], records.aggregate); });
}

} // namespace mst
