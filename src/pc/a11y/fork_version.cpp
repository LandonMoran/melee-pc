/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "fork_version.hpp"
#include "pc/version.hpp"

/* Set for this file alone by a11y.cmake, so a new version or label rebuilds
 * nothing else. */
#if !defined(A11Y_FORK_VERSION) || !defined(A11Y_BUILD_LABEL)
#error "Build with CMake so A11Y_FORK_VERSION matches src/pc/a11y/FORK_VERSION"
#endif

namespace a11y {

const BuildVersions& build_versions() {
    static const BuildVersions versions{A11Y_FORK_VERSION, A11Y_BUILD_LABEL, pc::get_app_version()};
    return versions;
}

std::string labelled_fork_version(const BuildVersions& versions) {
    if (versions.label.empty()) {
        return versions.fork;
    }
    return versions.fork + " (" + versions.label + ")";
}

std::string shown_version(const BuildVersions& versions) {
    const std::string base = "Melee-PC " + versions.app;
    if (versions.label.empty()) {
        return versions.fork + " (" + base + ")";
    }
    return versions.fork + " (" + versions.label + ", " + base + ")";
}

std::string version_line(const BuildVersions& versions) {
    return "Non-Visual Melee " + labelled_fork_version(versions) + ", Melee-PC " + versions.app;
}

}  // namespace a11y
