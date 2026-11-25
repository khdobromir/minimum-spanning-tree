#pragma once

#include "mst/algorithms/disjoint_set_union.hpp"
#include "mst/model/MinimumSpanningTree.hpp"

#include <functional>
#include <memory>

namespace mst {

enum class Algorithm { prim, kruskal };
enum class Decision { accepted, already_included, cycle };

template <WeightType W> struct TraceStep {
    Edge<W> edge;
    Decision decision;
    std::optional<typename WeightTraits<W>::Sum> subtotal;
    std::size_t accepted;
};

template <WeightType W> struct PrimStep {
    TraceStep<W> common;
    std::size_t included_vertices;
};

template <WeightType W> struct KruskalStep {
    TraceStep<W> common;
    std::size_t components;
};

template <WeightType W> using TraceEvent = std::variant<PrimStep<W>, KruskalStep<W>>;

#if MST_HAS_FUNCTION_REF
template <WeightType W> using TraceCallback = std::optional<std::function_ref<void(const TraceEvent<W>&)>>;
#else
template <WeightType W> class TraceCallback {
  public:
    TraceCallback() = default;

    template <typename F>
        requires(!std::same_as<std::remove_cvref_t<F>, TraceCallback> &&
                 std::invocable<const F&, const TraceEvent<W>&>)
    TraceCallback(F& callback)
        : object_{std::addressof(callback)}, invoke_{[](const void* object, const TraceEvent<W>& event) {
              std::invoke(*static_cast<const F*>(object), event);
          }} {}

    explicit operator bool() const { return invoke_ != nullptr; }
    const TraceCallback& operator*() const { return *this; }
    void operator()(const TraceEvent<W>& event) const { invoke_(object_, event); }

  private:
    const void* object_{};
    void (*invoke_)(const void*, const TraceEvent<W>&){};
};
#endif

template <WeightType W> struct BuildOptions {
    VertexId start{};
    std::pmr::memory_resource* scratch = std::pmr::get_default_resource();
    std::pmr::memory_resource* storage = std::pmr::get_default_resource();
    TraceCallback<W> trace{};
};

template <WeightType W> class IMstAlgorithm {
  public:
    virtual ~IMstAlgorithm() = default;

    // The callback is synchronous. The storage resource must outlive the result.
    [[nodiscard]] virtual Result<SpanningTree<W>> build(const Graph<W>& graph,
                                                        BuildOptions<W> options = {}) const = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

namespace detail {

template <WeightType W> class TraceProgress {
  public:
    [[nodiscard]] TraceStep<W> step(Edge<W> edge, Decision decision, std::size_t accepted) {
        if constexpr (std::integral<W>) {
            if (decision == Decision::accepted) {
                total_.add(edge.weight);
            }
            return {edge, decision, total_.narrow(), accepted};
        } else {
            if (representable_ && decision == Decision::accepted) {
                representable_ = total_.add(edge.weight).has_value();
            }
            using Sum = WeightTraits<W>::Sum;
            return {edge, decision, representable_ ? std::optional<Sum>{total_.value()} : std::nullopt,
                    accepted};
        }
    }

  private:
    std::conditional_t<std::integral<W>, ExactIntegerSum, WeightAccumulator<W>> total_;
    bool representable_{true};
};

} // namespace detail
} // namespace mst
