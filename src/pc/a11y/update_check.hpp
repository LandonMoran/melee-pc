/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "fork_version.hpp"
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

/* The launcher's update check follows the fork's releases, never the base
 * port's (docs/adr/0011-fork-releases-are-versioned-apart-from-the-base-
 * port.md): which release to offer from the fork repository's release list,
 * and the words the launcher and the port menu show for it.
 * update_hooks.cpp hands these to src/pc/updater.cpp and src/pc/launcher.cpp. */
namespace a11y::updates {

/* A release's tag is this and a whole fork version, "a11y-v0.1.1". */
inline constexpr std::string_view kTagPrefix = "a11y-v";

/* The error for a list GitHub refused, its own message left out. */
inline constexpr const char* kRefused = "GitHub refused the check, try again later.";

/* The release on offer. */
struct Offer {
    std::string version;  /* "0.1.1" */
    std::string tag;      /* "a11y-v0.1.1" */
    std::string name;     /* the release's title, "Non-Visual Melee 0.1.1 beta" */
    std::string page_url; /* the release's page on GitHub */
    std::string asset_name;
    std::string asset_url;
    std::size_t asset_size = 0;
};

struct Pick {
    /* Empty when no release is newer. */
    std::optional<Offer> offer;
    /* Why the list could not be read; empty when it could. */
    std::string error;
};

/* From the release list's JSON as GitHub's API gives it
 * (GET /repos/{owner}/{repo}/releases), the newest release that is
 * - tagged kTagPrefix and a whole version MAJOR.MINOR.PATCH, so ci-latest
 *   and the base port's tags are left out,
 * - newer than current, compared by number,
 * - neither a draft nor a prerelease, as the release workflow publishes a
 *   release once it is ready for every player,
 * - holding the zip named asset; an empty asset is a platform without zips.
 * A list that is not a JSON array is an error: GitHub's refusals, such as its
 * rate limit, are an object with a message, which becomes kRefused. */
Pick pick_release(std::string_view json, std::string_view current, std::string_view asset);

/* The fork's zip for a platform and architecture named as updater.cpp names
 * them ("Windows" or "macOS", "x86_64" or "arm64"), as .github/workflows/
 * a11y.yml names it: "NonVisualMelee-Windows-x86_64.zip". Empty for any
 * other platform, which the fork ships no zip for. */
std::string asset_name(std::string_view platform, std::string_view architecture);

/* "0.1.1" from "a11y-v0.1.1"; any other tag as it is. */
std::string version_of_tag(std::string_view tag);

/* The updater's status words, the launcher's Settings and the port menu
 * alike: "Non-Visual Melee is up to date (0.1.0)." and "Update available:
 * Non-Visual Melee 0.1.1". */
std::string up_to_date(const BuildVersions& versions);
std::string available(std::string_view tag);
/* The update banner's description, before the base port's own instructions:
 * "A newer version of Non-Visual Melee is available (0.1.1)." */
std::string description(std::string_view tag);

}  // namespace a11y::updates
