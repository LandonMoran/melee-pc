/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The update check's hand-offs from src/pc/updater.cpp and
 * src/pc/launcher.cpp (a11y_hooks.h), on update_check.cpp's choice and
 * words, and the launcher's Discord link. They hold whatever the
 * Accessibility switch says: a fork build follows the fork's releases and
 * leads to the fork's channel for every player. */
#include "a11y_hooks.h"
#include "fork_version.hpp"
#include "pc/pc.h"
#include "pc/updater.hpp"
#include "update_check.hpp"
#include <cstdlib>
#include <fstream>
#include <iterator>

/* The fork's repository on GitHub, where its releases are published. */
#define A11Y_REPOSITORY "ohylli/non-visual-melee"

namespace {

/* The platform and architecture as updater.cpp names them, for the zip. */
constexpr const char* kPlatform =
#if defined(_WIN32)
    "Windows";
#elif defined(__APPLE__)
    "macOS";
#else
    "";
#endif

constexpr const char* kArchitecture =
#if defined(__aarch64__) || defined(_M_ARM64)
    "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
    "x86_64";
#else
    "";
#endif

/* MELEE_A11Y_UPDATE_LIST, a developer switch: a file holding a release list
 * in the API's shape, read in place of GitHub's, so a drive run hears an
 * update, or none, from a canned list (tools/a11y/update_lists/). */
const char* list_file() {
    const char* path = std::getenv("MELEE_A11Y_UPDATE_LIST");
    return path != nullptr && path[0] != '\0' ? path : nullptr;
}

const std::string& tag_of(const pc::updater::UpdateState& state) {
    return state.latest_release.tag_name;
}

}  // namespace

const wchar_t* pc_a11y_update_api_path() {
    return L"/repos/" A11Y_REPOSITORY "/releases";
}

const char* pc_a11y_update_api_url() {
    return "https://api.github.com/repos/" A11Y_REPOSITORY "/releases";
}

const char* pc_a11y_update_releases_page() {
    return "https://github.com/" A11Y_REPOSITORY "/releases/latest";
}

/* Non-Visual Melee's channel on the accessibility modding server, also
 * linked from ACCESSIBILITY.md. */
const char* pc_a11y_discord_url() {
    return "https://discord.gg/muRgv2GYHe";
}

bool pc_a11y_update_list_file(std::string& body) {
    const char* path = list_file();
    if (path == nullptr) {
        return false;
    }
    /* Left empty when unreadable, which pc_a11y_update_result reports. */
    std::ifstream in(path, std::ios::binary);
    body.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

void pc_a11y_update_result(const std::string& body, pc::updater::UpdateState& state) {
    namespace updates = a11y::updates;
    using pc::updater::Status;
    const a11y::BuildVersions& versions = a11y::build_versions();
    const char* canned = list_file();
    updates::Pick pick;
    if (canned != nullptr && body.empty()) {
        pick.error = std::string("no release list in ") + canned;
    } else {
        pick = updates::pick_release(
            body, versions.fork, updates::asset_name(kPlatform, kArchitecture));
    }
    state.latest_release = {};
    state.target_asset_name.clear();
    state.target_asset_url.clear();
    state.download_total_bytes = 0;
    if (!pick.error.empty()) {
        state.status = Status::Failed;
        state.message = "Could not check for updates: " + pick.error;
    } else if (!pick.offer) {
        state.status = Status::UpToDate;
        state.message = updates::up_to_date(versions);
    } else {
        updates::Offer& offer = *pick.offer;
        state.latest_release.tag_name = offer.tag;
        state.latest_release.name = offer.name;
        state.latest_release.html_url = offer.page_url;
        state.target_asset_name = offer.asset_name;
        state.target_asset_url = offer.asset_url;
        state.download_total_bytes = offer.asset_size;
        state.status = Status::UpdateAvailable;
        state.message = updates::available(offer.tag);
    }
    pc_log_line("[a11y] update check%s: %s", canned != nullptr ? " (canned list)" : "",
        state.message.c_str());
}

std::string pc_a11y_shown_version() {
    return a11y::shown_version(a11y::build_versions());
}

std::string pc_a11y_update_version(const pc::updater::UpdateState& state) {
    return a11y::updates::version_of_tag(tag_of(state));
}

std::string pc_a11y_update_available(const pc::updater::UpdateState& state) {
    return a11y::updates::available(tag_of(state));
}

std::string pc_a11y_update_description(const pc::updater::UpdateState& state) {
    return a11y::updates::description(tag_of(state));
}

std::string pc_a11y_update_up_to_date() {
    return a11y::updates::up_to_date(a11y::build_versions());
}
