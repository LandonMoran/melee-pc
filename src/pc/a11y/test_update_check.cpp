/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The update check's choice of release, on canned release lists in the shape
 * GitHub's API gives them, and its words; then the hand-offs to updater.cpp
 * (update_hooks.cpp) filling the updater's state, with a canned list read
 * through MELEE_A11Y_UPDATE_LIST. Supplies the pc_log_line that main.c
 * normally provides. */
#include "a11y_hooks.h"
#include "pc/updater.hpp"
#include "update_check.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using a11y::BuildVersions;
using a11y::updates::Pick;
using a11y::updates::pick_release;

constexpr const char* kZip = "NonVisualMelee-Windows-x86_64.zip";

/* One release in the API's shape, with the fields the check reads and a few
 * it skips. */
std::string release(const std::string& tag, const std::string& zip = kZip, bool draft = false,
    bool prerelease = false) {
    std::string assets;
    if (!zip.empty()) {
        assets =
            R"({"name": ")" + zip +
            R"(", "size": 123456789, "content_type": "application/zip",)"
            R"( "browser_download_url": "https://github.com/ohylli/non-visual-melee/releases/download/)" +
            tag + "/" + zip + R"("})";
    }
    return R"({"url": "https://api.github.com/repos/ohylli/non-visual-melee/releases/1", "id": 1,)"
           R"( "html_url": "https://github.com/ohylli/non-visual-melee/releases/tag/)" +
           tag + R"(", "tag_name": ")" + tag + R"(", "name": "Release \")" + tag +
           R"(\" \u00e9",)"
           R"( "draft": )" +
           (draft ? "true" : "false") + R"(, "prerelease": )" + (prerelease ? "true" : "false") +
           R"(, "published_at": null, "author": {"login": "ohylli", "site_admin": false},)"
           R"( "body": "Notes\nwith [a link](https://example.com).", "assets": [)" +
           assets + "]}";
}

std::string list(std::initializer_list<std::string> releases) {
    std::string json = "[\n";
    bool first = true;
    for (const auto& r : releases) {
        json += (first ? "  " : ",\n  ") + r;
        first = false;
    }
    return json + "\n]\n";
}

Pick pick(const std::string& json, const std::string& current = "0.1.0") {
    return pick_release(json, current, kZip);
}

void a_newer_release_is_offered() {
    const Pick p = pick(list({release("a11y-v0.1.1")}));
    assert(p.error.empty());
    assert(p.offer);
    assert(p.offer->version == "0.1.1");
    assert(p.offer->tag == "a11y-v0.1.1");
    assert(p.offer->name == "Release \"a11y-v0.1.1\" \xc3\xa9");
    assert(
        p.offer->page_url == "https://github.com/ohylli/non-visual-melee/releases/tag/a11y-v0.1.1");
    assert(p.offer->asset_name == kZip);
    assert(p.offer->asset_url ==
           "https://github.com/ohylli/non-visual-melee/releases/download/a11y-v0.1.1/"
           "NonVisualMelee-Windows-x86_64.zip");
    assert(p.offer->asset_size == 123456789);
}

void equal_and_older_releases_are_not() {
    assert(!pick(list({release("a11y-v0.1.0")})).offer);
    assert(!pick(list({release("a11y-v0.0.9"), release("a11y-v0.1.0")})).offer);
    assert(!pick(list({release("a11y-v0.1.0")}), "0.2.0").offer);
    assert(pick(list({release("a11y-v0.1.0")}), "0.2.0").error.empty());
}

void versions_compare_by_number_not_text() {
    assert(pick(list({release("a11y-v0.10.0")}), "0.9.0").offer);
    assert(pick(list({release("a11y-v1.0.0")}), "0.99.99").offer);
    assert(!pick(list({release("a11y-v0.9.0")}), "0.10.0").offer);
    assert(pick(list({release("a11y-v0.1.10")}), "0.1.9").offer);
}

void the_newest_is_offered_whatever_the_order() {
    const Pick p =
        pick(list({release("a11y-v0.1.1"), release("a11y-v0.3.0"), release("a11y-v0.2.0")}));
    assert(p.offer && p.offer->version == "0.3.0");
}

void ci_latest_and_base_port_tags_are_skipped() {
    const std::string json = list({release("ci-latest", kZip, false, true), release("v0.4.3"),
        release("v9.9.9"), release("0.9.0"), release("a11y-0.9.0"), release("A11Y-V0.9.0")});
    const Pick p = pick(json);
    assert(p.error.empty());
    assert(!p.offer);
}

void a_tag_must_be_a_whole_version() {
    for (const char* tag :
        {"a11y-v0.2", "a11y-v0.2.0-rc1", "a11y-v0.2.0.1", "a11y-v.2.0", "a11y-v0..0", "a11y-v0.2.x",
            "a11y-v", "a11y-v 0.2.0", "a11y-v0.2.0 ", "a11y-v99999999999999999999.0.0"})
    {
        assert(!pick(list({release(tag)})).offer);
    }
}

void a_release_without_this_platforms_zip_is_skipped() {
    assert(!pick(list({release("a11y-v0.2.0", "")})).offer);
    assert(!pick(list({release("a11y-v0.2.0", "NonVisualMelee-macOS-arm64.zip")})).offer);
    assert(!pick(list({release("a11y-v0.2.0", "Melee-Windows-x86_64.zip")})).offer);
    /* The newest with the zip, past a newer one without. */
    const Pick p = pick(list({release("a11y-v0.3.0", ""), release("a11y-v0.2.0")}));
    assert(p.offer && p.offer->version == "0.2.0");
    /* No zip name at all: a platform with no zips is never offered one. */
    assert(!pick_release(list({release("a11y-v0.2.0")}), "0.1.0", "").offer);
}

void drafts_and_prereleases_are_skipped() {
    assert(!pick(list({release("a11y-v0.2.0", kZip, true)})).offer);
    assert(!pick(list({release("a11y-v0.2.0", kZip, false, true)})).offer);
    const Pick p = pick(list({release("a11y-v0.3.0", kZip, true), release("a11y-v0.2.0")}));
    assert(p.offer && p.offer->version == "0.2.0");
}

void no_release_at_all() {
    for (const char* json : {"[]", " [ ] \n"}) {
        const Pick p = pick(json);
        assert(p.error.empty());
        assert(!p.offer);
    }
}

void an_unreadable_list_is_an_error() {
    for (const char* json :
        {"", "[", "[{\"tag_name\": \"a11y-v0.2.0\"", "{\"tag_name\": ", "nonsense", "[] trailing"})
    {
        const Pick p = pick(json);
        assert(!p.error.empty());
        assert(!p.offer);
    }
    /* GitHub's refusal, such as its rate limit, is an object with a message. */
    const Pick refused = pick(
        R"({"message": "API rate limit exceeded for 1.2.3.4.", "documentation_url": "https://docs.github.com"})");
    assert(!refused.offer);
    assert(refused.error == a11y::updates::kRefused);
    assert(refused.error.find("1.2.3.4") == std::string::npos);
    assert(!pick(R"({"id": 1})").error.empty());
}

void escapes_are_decoded() {
    const std::string json =
        R"([{"tag_name": "a11y-v0.2.0", "draft": false, "prerelease": false,)"
        R"( "html_url": "https:\/\/example.com\/a", "name": "\ud83c\udfae \t\\",)"
        R"( "assets": [{"name": "NonVisualMelee-Windows-x86_64.zip",)"
        R"( "browser_download_url": "https://example.com/z", "size": 1.5e3}]}])";
    const Pick p = pick(json);
    assert(p.offer);
    assert(p.offer->page_url == "https://example.com/a");
    assert(p.offer->name == "\xf0\x9f\x8e\xae \t\\");
    assert(p.offer->asset_size == 1500);
    /* A lone surrogate and a NUL become U+FFFD; a size past any zip's is left out. */
    const Pick odd = pick(R"([{"tag_name": "a11y-v0.2.0", "name": "a\ud83cb\u0000c\udfae",)"
                          R"( "assets": [{"name": "NonVisualMelee-Windows-x86_64.zip",)"
                          R"( "browser_download_url": "https://example.com/z", "size": 1e999}]}])");
    assert(odd.offer);
    assert(odd.offer->name == "a\xef\xbf\xbd"
                              "b\xef\xbf\xbd"
                              "c\xef\xbf\xbd");
    assert(odd.offer->asset_size == 0);
}

void the_zip_for_each_platform() {
    using a11y::updates::asset_name;
    assert(asset_name("Windows", "x86_64") == "NonVisualMelee-Windows-x86_64.zip");
    assert(asset_name("macOS", "arm64") == "NonVisualMelee-macOS-arm64.zip");
    assert(asset_name("macOS", "x86_64") == "NonVisualMelee-macOS-x86_64.zip");
    assert(asset_name("Linux", "x86_64").empty());
    assert(asset_name("Windows", "").empty());
    assert(asset_name("", "").empty());
}

void the_words() {
    using namespace a11y::updates;
    const BuildVersions build{"0.1.0", "local build", "v0.4.2"};
    assert(up_to_date(build) == "Non-Visual Melee is up to date (0.1.0).");
    assert(up_to_date(BuildVersions{"0.1.0", "", "v0.4.2"}) ==
           "Non-Visual Melee is up to date (0.1.0).");
    assert(version_of_tag("a11y-v0.1.1") == "0.1.1");
    assert(version_of_tag("v0.4.3") == "v0.4.3");
    assert(version_of_tag("") == "");
    assert(available("a11y-v0.1.1") == "Update available: Non-Visual Melee 0.1.1");
    assert(
        description("a11y-v0.1.1") == "A newer version of Non-Visual Melee is available (0.1.1).");
}

void set_list_file(const std::string& path) {
#if defined(_WIN32)
    _putenv_s("MELEE_A11Y_UPDATE_LIST", path.c_str());
#else
    setenv("MELEE_A11Y_UPDATE_LIST", path.c_str(), 1);
#endif
}

std::string g_logged;

/* This build's platform and architecture, as update_hooks.cpp names them. */
constexpr const char* kThisPlatform =
#if defined(_WIN32)
    "Windows";
#elif defined(__APPLE__)
    "macOS";
#else
    "";
#endif
constexpr const char* kThisArchitecture =
#if defined(__aarch64__) || defined(_M_ARM64)
    "arm64";
#else
    "x86_64";
#endif

/* On a platform with a zip a canned list offers it; the version is far past
 * any FORK_VERSION. */
void the_hooks_fill_the_updaters_state(const std::string& folder) {
    using pc::updater::Status;
    using pc::updater::UpdateState;
    const std::string zip = a11y::updates::asset_name(kThisPlatform, kThisArchitecture);
    std::string body = "untouched";
    set_list_file("");
    assert(!pc_a11y_update_list_file(body));
    assert(body == "untouched");

    const std::string path = folder + "/update_list.json";
    {
        std::ofstream out(path, std::ios::binary);
        out << list({release("ci-latest", zip, false, true), release("a11y-v99.0.0", zip)});
    }
    set_list_file(path);
    assert(pc_a11y_update_list_file(body));
    UpdateState state;
    state.message = "Checking for updates...";
    pc_a11y_update_result(body, state);
    if (zip.empty()) {
        assert(state.status == Status::UpToDate);
    } else {
        assert(state.status == Status::UpdateAvailable);
        assert(state.message == "Update available: Non-Visual Melee 99.0.0");
        assert(state.latest_release.tag_name == "a11y-v99.0.0");
        assert(state.latest_release.html_url ==
               "https://github.com/ohylli/non-visual-melee/releases/tag/a11y-v99.0.0");
        assert(state.target_asset_name == zip);
        assert(state.target_asset_url ==
               "https://github.com/ohylli/non-visual-melee/releases/download/a11y-v99.0.0/" + zip);
        assert(state.download_total_bytes == 123456789);
        assert(pc_a11y_update_version(state) == "99.0.0");
        assert(pc_a11y_update_available(state) == "Update available: Non-Visual Melee 99.0.0");
        assert(pc_a11y_update_description(state) ==
               "A newer version of Non-Visual Melee is available (99.0.0).");
        assert(g_logged ==
               "[a11y] update check (canned list): Update available: Non-Visual Melee 99.0.0");
    }

    /* None newer: up to date, and the last offer gone. */
    pc_a11y_update_result(list({release("v0.4.3")}), state);
    assert(state.status == Status::UpToDate);
    assert(state.message == pc_a11y_update_up_to_date());
    assert(state.latest_release.tag_name.empty());
    assert(state.target_asset_url.empty() && state.download_total_bytes == 0);

    /* A canned list missing reads as empty and is reported. */
    set_list_file(folder + "/no_such_list.json");
    body = "stale";
    assert(pc_a11y_update_list_file(body));
    assert(body.empty());
    pc_a11y_update_result(body, state);
    assert(state.status == Status::Failed);
    assert(state.message ==
           "Could not check for updates: no release list in " + folder + "/no_such_list.json");

    /* GitHub's refusal is a failure, not "up to date". */
    set_list_file("");
    pc_a11y_update_result(R"({"message": "Not Found"})", state);
    assert(state.status == Status::Failed);
    const std::string refused =
        std::string("Could not check for updates: ") + a11y::updates::kRefused;
    assert(state.message == refused);
    assert(g_logged == "[a11y] update check: " + refused);
}

void the_hooks_name_the_fork() {
    assert(std::wstring(pc_a11y_update_api_path()) == L"/repos/ohylli/non-visual-melee/releases");
    assert(std::string(pc_a11y_update_api_url()) ==
           "https://api.github.com/repos/ohylli/non-visual-melee/releases");
    assert(std::string(pc_a11y_update_releases_page()) ==
           "https://github.com/ohylli/non-visual-melee/releases/latest");
    assert(pc_a11y_shown_version().find(" (") != std::string::npos);
    assert(pc_a11y_shown_version().find("Melee-PC v") != std::string::npos);
}

}  // namespace

extern "C" void pc_log_line(const char* fmt, ...) {
    char line[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);
    g_logged = line;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: update_check_test <scratch folder>\n";
        return 2;
    }
    a_newer_release_is_offered();
    equal_and_older_releases_are_not();
    versions_compare_by_number_not_text();
    the_newest_is_offered_whatever_the_order();
    ci_latest_and_base_port_tags_are_skipped();
    a_tag_must_be_a_whole_version();
    a_release_without_this_platforms_zip_is_skipped();
    drafts_and_prereleases_are_skipped();
    no_release_at_all();
    an_unreadable_list_is_an_error();
    escapes_are_decoded();
    the_zip_for_each_platform();
    the_words();
    the_hooks_name_the_fork();
    std::filesystem::create_directories(argv[1]);
    the_hooks_fill_the_updaters_state(argv[1]);
    std::cout << "update_check: all tests passed\n";
    return 0;
}
