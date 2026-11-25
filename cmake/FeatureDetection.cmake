include(CheckCXXSourceCompiles)
include(CMakePushCheckState)

set(MST_PROFILE Portable)
if(MST_FULL_CPP26)
    set(MST_PROFILE "Full C++26")
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS -freflection)
    check_cxx_source_compiles([=[
        #include <meta>
        #if !defined(__cpp_impl_reflection) || !defined(__cpp_lib_reflection)
        #error Reflection unavailable
        #endif
        enum class Flag { first, second };
        consteval auto probe() {
            template for (constexpr auto member : std::define_static_array(std::meta::enumerators_of(^^Flag)))
                if ([:member:] == Flag::second)
                    return std::meta::identifier_of(member);
            return std::string_view{};
        }
        static_assert(probe() == "second");
        int main() {}
    ]=] MST_HAS_REFLECTION)
    cmake_pop_check_state()
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_FLAGS "-fcontracts -fcontract-evaluation-semantic=enforce")
    check_cxx_source_compiles([=[
        #if !defined(__cpp_contracts)
        #error Contracts unavailable
        #endif
        int twice(int x) pre(x > 0) post(result: result > 0) {
            contract_assert(x < 10);
            return x + x;
        }
        int main() { return twice(2) == 4 ? 0 : 1; }
    ]=] MST_HAS_CONTRACTS)
    cmake_pop_check_state()
    if(NOT MST_HAS_REFLECTION OR NOT MST_HAS_CONTRACTS)
        message(FATAL_ERROR "Full C++26 requires working reflection and enforced contracts. Select Portable for this compiler.")
    endif()
    list(APPEND MST_EXPERIMENTAL_FLAGS -freflection -fcontracts -fcontract-evaluation-semantic=enforce)
    list(APPEND MST_EXPERIMENTAL_LINK_FLAGS -fcontracts)
else()
    set(MST_HAS_REFLECTION OFF)
    set(MST_HAS_CONTRACTS OFF)
endif()

check_cxx_source_compiles([=[
    #include <functional>
    #if !defined(__cpp_lib_function_ref)
    #error function_ref unavailable
    #endif
    void call(std::function_ref<void()> f) { f(); }
    int main() { call([] {}); }
]=] MST_HAS_FUNCTION_REF)
check_cxx_source_compiles([=[
    #include <span>
    #if !defined(__cpp_lib_span) || __cpp_lib_span < 202311L
    #error span::at unavailable
    #endif
    static_assert(std::span{"a"}.at(0) == 'a');
    int main() {}
]=] MST_HAS_SPAN_AT)
check_cxx_source_compiles([=[
    #include <optional>
    #include <ranges>
    #if !defined(__cpp_lib_optional_range_support)
    #error optional range unavailable
    #endif
    static_assert(std::ranges::size(std::optional{3}) == 1);
    int main() {}
]=] MST_HAS_OPTIONAL_RANGE)
cmake_push_check_state(RESET)
set(CMAKE_REQUIRED_FLAGS -Werror=deprecated-declarations)
check_cxx_source_compiles([=[
    #include <inplace_vector>
    #include <string_view>
    #if !defined(__cpp_lib_inplace_vector)
    #error inplace_vector unavailable
    #endif
    constexpr bool test() {
        std::inplace_vector<int, 3> values;
        values.push_back(2);
        return values.front() == 2;
    }
    static_assert(test());
    struct Token { std::string_view value; std::size_t column; };
    int main() {
        std::inplace_vector<Token, 2> tokens;
        tokens.push_back({"a", 1});
        return tokens.front().column == 1 ? 0 : 1;
    }
]=] MST_HAS_INPLACE_VECTOR)
cmake_pop_check_state()
check_cxx_source_compiles([=[
    #include <random>
    #if !defined(__cpp_lib_philox_engine)
    #error Philox unavailable
    #endif
    int main() { std::philox4x64 engine{42}; return engine() == 0; }
]=] MST_HAS_PHILOX)
check_cxx_source_compiles([=[
    #include <stdckdint.h>
    #include <cstdint>
    #if !defined(__cpp_lib_stdckdint_h)
    #error C++ checked arithmetic unavailable
    #endif
    int main() {
        std::int64_t out = 0;
        return !ckd_add(&out, std::int64_t{2}, std::int64_t{3}) && out == 5 ? 0 : 1;
    }
]=] MST_HAS_CHECKED_ADD)
check_cxx_source_compiles([=[
    #include <variant>
    #if !defined(__cpp_lib_variant) || __cpp_lib_variant < 202306L
    #error variant::visit unavailable
    #endif
    static_assert(std::variant<int>{3}.visit([](int n) { return n; }) == 3);
    int main() {}
]=] MST_HAS_VARIANT_VISIT)
check_cxx_source_compiles([=[
    #include <algorithm>
    #include <ranges>
    #if !defined(__cpp_lib_algorithm_default_value_type)
    #error default value type unavailable
    #endif
    struct Point { int x; constexpr bool operator==(const Point&) const = default; };
    constexpr Point points[] = {{1}};
    static_assert(std::ranges::contains(points, {1}));
    int main() {}
]=] MST_HAS_RANGES_DEFAULT_VALUE)
string(TOUPPER "${CMAKE_BUILD_TYPE}" MST_BUILD_TYPE_UPPER)
string(JOIN " " MST_BUILD_FLAGS "${CMAKE_CXX_FLAGS}" "${CMAKE_CXX_FLAGS_${MST_BUILD_TYPE_UPPER}}"
    ${MST_EXPERIMENTAL_FLAGS})
string(STRIP "${MST_BUILD_FLAGS}" MST_BUILD_FLAGS)
string(REPLACE [[\]] [[\\]] MST_BUILD_FLAGS "${MST_BUILD_FLAGS}")
string(REPLACE [["]] [[\"]] MST_BUILD_FLAGS "${MST_BUILD_FLAGS}")
configure_file(cmake/features.hpp.in generated/mst/features.hpp @ONLY)
