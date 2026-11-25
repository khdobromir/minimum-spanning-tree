#pragma once

#include "mst/features.hpp"

#include <array>
#include <cassert>
#include <span>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <variant>

#if MST_HAS_REFLECTION
#include <meta>
#endif

#if MST_HAS_CONTRACTS
#define MST_ASSERT(expression) contract_assert(expression)
#else
#define MST_ASSERT(expression) assert(expression)
#endif

namespace mst::detail {

template <typename T> constexpr T& span_at(std::span<T> items, std::size_t index) {
#if MST_HAS_SPAN_AT
    return items.at(index);
#else
    if (index >= items.size()) {
        throw std::out_of_range{"span index"};
    }
    return items[index];
#endif
}

template <typename Visitor, typename Variant>
constexpr decltype(auto) visit(Visitor&& visitor, Variant&& variant) {
#if MST_HAS_VARIANT_VISIT
    return std::forward<Variant>(variant).visit(std::forward<Visitor>(visitor));
#else
    return std::visit(std::forward<Visitor>(visitor), std::forward<Variant>(variant));
#endif
}

#if MST_HAS_REFLECTION
template <typename Enum> consteval auto enum_names() {
    constexpr auto members = std::define_static_array(std::meta::enumerators_of(^^Enum));
    std::array<std::string_view, members.size()> names{};
    for (std::size_t i = 0; i < members.size(); ++i) {
        names[i] = std::meta::identifier_of(members[i]);
    }
    return names;
}
#endif

} // namespace mst::detail
