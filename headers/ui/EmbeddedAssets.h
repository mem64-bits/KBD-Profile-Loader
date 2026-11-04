#pragma once

#include <span>
#include <string_view>

// Files from assets/ compiled into the executable by CMake (see KBD_EMBEDDED_ASSETS in CMakeLists.txt).
namespace ui::assets {

    // The bytes of an embedded file by its path relative to the repository ("assets/controller/..."), or an
    // empty span when it was not embedded.
    [[nodiscard]] std::span<const unsigned char> find(std::string_view path) noexcept;

} // namespace ui::assets
