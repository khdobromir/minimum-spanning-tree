#pragma once

#include "mst/features.hpp"
#include "mst/utility/error.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>

#if MST_HAS_CHECKED_ADD
#include <stdckdint.h>
#endif

namespace mst {

namespace detail {

// Two words hold a sum of up to 2^64 - 1 int64 weights on the target platform.
class ExactIntegerSum {
  public:
    constexpr void add(std::int64_t value) {
        const auto previous = low_;
        low_ += static_cast<std::uint64_t>(value);
        high_ += static_cast<std::int64_t>(low_ < previous) - static_cast<std::int64_t>(value < 0);
    }

    [[nodiscard]] constexpr std::optional<std::int64_t> narrow() const {
        const auto value = std::bit_cast<std::int64_t>(low_);
        return high_ == (value < 0 ? -1 : 0) ? std::optional{value} : std::nullopt;
    }

    constexpr auto operator<=>(const ExactIntegerSum&) const = default;

  private:
    std::int64_t high_{};
    std::uint64_t low_{};
};

} // namespace detail

template <typename W>
concept WeightType = std::same_as<W, std::int64_t> || std::same_as<W, double>;

[[nodiscard]] constexpr auto checked_add(std::int64_t left, std::int64_t right)
    -> std::optional<std::int64_t> {
#if MST_HAS_CHECKED_ADD
    if !consteval {
        std::int64_t sum{};
        if (ckd_add(&sum, left, right)) {
            return std::nullopt;
        }
        return sum;
    }
#endif
    using Limits = std::numeric_limits<std::int64_t>;
    if ((right > 0 && left > Limits::max() - right) || (right < 0 && left < Limits::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

template <WeightType W> struct WeightTraits {
    using Sum = std::conditional_t<std::integral<W>, W, long double>;

    [[nodiscard]] static constexpr bool finite(W weight) {
        if constexpr (std::floating_point<W>) {
            return std::isfinite(weight);
        }
        return true;
    }

    [[nodiscard]] static bool equivalent(Sum left, Sum right) {
        if constexpr (std::floating_point<W>) {
            constexpr long double absolute = 1e-12L;
            constexpr long double relative = 1e-12L;
            return std::isfinite(left) && std::isfinite(right) &&
                   std::abs(left - right) <= absolute + relative * std::max(std::abs(left), std::abs(right));
        }
        return left == right;
    }
};

template <WeightType W> class WeightAccumulator {
  public:
    using Sum = WeightTraits<W>::Sum;

    [[nodiscard]] Result<void> add(W weight) {
        if constexpr (std::integral<W>) {
            const auto sum = checked_add(sum_, weight);
            if (!sum) {
                return fail(ErrorCode::weight_overflow, "int64 running total is out of range");
            }
            sum_ = *sum;
        } else {
            const auto term = static_cast<Sum>(weight);
            const auto next = sum_ + term;
            // Neumaier's correction keeps small contributions through cancellation.
            const auto correction =
                std::abs(sum_) >= std::abs(term) ? (sum_ - next) + term : (term - next) + sum_;
            const auto corrected = compensation_ + correction;
            if (!std::isfinite(next) || !std::isfinite(corrected) || !std::isfinite(next + corrected)) {
                return fail(ErrorCode::weight_overflow, "long double running total is not finite");
            }
            sum_ = next;
            compensation_ = corrected;
        }
        return {};
    }

    [[nodiscard]] Sum value() const {
        if constexpr (std::floating_point<W>) {
            return sum_ + compensation_;
        }
        return sum_;
    }

  private:
    Sum sum_{};
    [[no_unique_address]] std::conditional_t<std::floating_point<W>, Sum, std::monostate> compensation_{};
};

} // namespace mst
