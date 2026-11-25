#pragma once

#include "mst/benchmark/graph_generator.hpp"

#include <filesystem>
#include <variant>

namespace mst {

enum class WeightKind { int64, double_precision };

[[nodiscard]] inline Result<WeightKind> parse_weight_kind(std::string_view value) {
    if (value == "int64") {
        return WeightKind::int64;
    }
    if (value == "double") {
        return WeightKind::double_precision;
    }
    return fail(ErrorCode::invalid_option, "weight type must be int64 or double");
}

template <WeightType W> [[nodiscard]] constexpr std::string_view weight_kind_name() {
    if constexpr (std::integral<W>) {
        return "int64";
    }
    return "double";
}

template <typename Enum, std::size_t N>
    requires std::is_enum_v<Enum>
[[nodiscard]] Result<Enum> choice(std::string_view value, const std::array<std::string_view, N>& names) {
    const auto position = std::ranges::find(names, value);
    if (position == names.end()) {
        return fail(ErrorCode::invalid_option, "unknown value '" + std::string{value} + "'");
    }
    return static_cast<Enum>(position - names.begin());
}

template <WeightType W> struct FileExperiment {
    using Weight = W;
    std::filesystem::path path;
};

using Experiment = std::variant<GenerationOptions<std::int64_t>, GenerationOptions<double>,
                                FileExperiment<std::int64_t>, FileExperiment<double>>;

struct BenchmarkConfig {
    std::size_t repetitions{11};
    std::size_t warmups{2};
    std::size_t memory_limit_bytes{default_memory_limit};
    std::size_t exchange_limit{2'000'000};
    VertexId start{};
    std::optional<std::pair<VertexId, VertexId>> path{};
    std::vector<Experiment> cases;
    std::filesystem::path source{};
};

class BenchmarkRunner {
  public:
    explicit BenchmarkRunner(BenchmarkConfig config) : config_{std::move(config)} {}

    [[nodiscard]] static Result<BenchmarkConfig> read_config(const std::filesystem::path& path);
    [[nodiscard]] Result<void> run(const std::filesystem::path& output) const;

  private:
    BenchmarkConfig config_;
};

} // namespace mst
