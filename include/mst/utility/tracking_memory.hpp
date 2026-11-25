#pragma once

#include "mst/utility/features.hpp"

#include <algorithm>
#include <limits>
#include <memory_resource>

namespace mst {

struct MemoryStats {
    std::size_t current_bytes{};
    std::size_t peak_bytes{};
    std::size_t total_bytes{};
    std::size_t allocation_count{};
    std::size_t deallocation_count{};
};

class TrackingMemoryResource final : public std::pmr::memory_resource {
  public:
    explicit TrackingMemoryResource(std::pmr::memory_resource* upstream = std::pmr::get_default_resource())
        : upstream_{upstream} {}

    TrackingMemoryResource(const TrackingMemoryResource&) = delete;
    TrackingMemoryResource& operator=(const TrackingMemoryResource&) = delete;

    [[nodiscard]] MemoryStats stats() const { return stats_; }

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override {
        constexpr auto max = std::numeric_limits<std::size_t>::max();
        if (bytes > max - stats_.total_bytes || stats_.allocation_count == max) {
            throw std::bad_alloc{};
        }
        auto* result = upstream_->allocate(bytes, alignment);
        stats_.current_bytes += bytes;
        stats_.peak_bytes = std::max(stats_.peak_bytes, stats_.current_bytes);
        stats_.total_bytes += bytes;
        ++stats_.allocation_count;
        return result;
    }

    void do_deallocate(void* address, std::size_t bytes, std::size_t alignment) override {
        MST_ASSERT(bytes <= stats_.current_bytes);
        upstream_->deallocate(address, bytes, alignment);
        stats_.current_bytes -= bytes;
        ++stats_.deallocation_count;
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }

    std::pmr::memory_resource* upstream_;
    MemoryStats stats_;
};

} // namespace mst
