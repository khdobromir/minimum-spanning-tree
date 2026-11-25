#pragma once

#include "mst/utility/text.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace mst::detail {

struct TemporaryOutput {
    std::filesystem::path path;

    ~TemporaryOutput() {
        if (!path.empty()) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    }
};

template <typename Writer>
[[nodiscard]] Result<void> write_file(const std::filesystem::path& destination, Writer writer) {
    const auto timestamp = number_text(std::chrono::steady_clock::now().time_since_epoch().count());
    for (int attempt = 0; attempt < 16; ++attempt) {
        auto sibling =
            destination.parent_path() / (".mst-" + timestamp + "-" + number_text(attempt) + ".tmp");
        std::ofstream output{sibling, std::ios::out | std::ios::noreplace};
        std::error_code error;
        if (!output) {
            if (std::filesystem::exists(sibling, error)) {
                continue;
            }
            return fail(ErrorCode::write_error, destination.string());
        }
        TemporaryOutput temporary{std::move(sibling)};
        auto result = writer(output);
        output.close();
        if (!result || !output) {
            return fail(ErrorCode::write_error, destination.string());
        }
        std::filesystem::rename(temporary.path, destination, error);
        if (error) {
            return fail(ErrorCode::write_error, destination.string() + ": " + error.message());
        }
        temporary.path.clear();
        return {};
    }
    return fail(ErrorCode::write_error, destination.string() + ": no temporary name available");
}

} // namespace mst::detail
