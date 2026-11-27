/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The fork's versions and build label: the logged and shown forms, with and
 * without a label, and this build's values against src/pc/a11y/FORK_VERSION, whose path is
 * the test's argument. */
#include "fork_version.hpp"
#include "pc/version.hpp"
#include <cassert>
#include <cctype>
#include <fstream>
#include <iostream>
#include <string>

namespace {

using a11y::BuildVersions;

void labelled_version_names_the_label() {
    assert(a11y::labelled_fork_version(BuildVersions{"0.1.0", "local build", "v0.4.2"}) ==
           "0.1.0 (local build)");
    assert(a11y::labelled_fork_version(BuildVersions{"0.1.0", "dev build 1a2b3c4", "v0.4.2"}) ==
           "0.1.0 (dev build 1a2b3c4)");
}

void a_release_build_has_no_label() {
    assert(a11y::labelled_fork_version(BuildVersions{"0.1.0", "", "v0.4.2"}) == "0.1.0");
    assert(a11y::version_line(BuildVersions{"0.1.0", "", "v0.4.2"}) ==
           "Non-Visual Melee 0.1.0, Melee-PC v0.4.2");
}

void version_line_names_both_versions() {
    assert(a11y::version_line(BuildVersions{"0.1.0", "local build", "v0.4.2"}) ==
           "Non-Visual Melee 0.1.0 (local build), Melee-PC v0.4.2");
    assert(a11y::version_line(BuildVersions{"0.2.3", "dev build 1a2b3c4", "v0.5.0"}) ==
           "Non-Visual Melee 0.2.3 (dev build 1a2b3c4), Melee-PC v0.5.0");
}

void shown_version_names_the_base_port_release() {
    assert(a11y::shown_version(BuildVersions{"0.1.0", "", "v0.4.2"}) == "0.1.0 (Melee-PC v0.4.2)");
    assert(a11y::shown_version(BuildVersions{"0.1.0", "local build", "v0.4.2"}) ==
           "0.1.0 (local build, Melee-PC v0.4.2)");
    assert(a11y::shown_version(BuildVersions{"0.2.3", "dev build 1a2b3c4", "v0.5.0"}) ==
           "0.2.3 (dev build 1a2b3c4, Melee-PC v0.5.0)");
}

bool is_version(const std::string& s) {
    int parts = 0;
    size_t i = 0;
    while (i < s.size()) {
        size_t digits = 0;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
            ++i;
            ++digits;
        }
        if (digits == 0) {
            return false;
        }
        ++parts;
        if (i < s.size()) {
            if (s[i] != '.') {
                return false;
            }
            ++i;
            if (i == s.size()) {
                return false;
            }
        }
    }
    return parts == 3;
}

void this_build_carries_the_version_file(const char* version_file) {
    std::ifstream in(version_file);
    assert(in);
    std::string written;
    std::getline(in, written);
    if (!written.empty() && written.back() == '\r') {
        written.pop_back();
    }
    const BuildVersions& build = a11y::build_versions();
    assert(build.fork == written);
    assert(is_version(build.fork));
    assert(build.app == pc::get_app_version());
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: fork_version_test <path of src/pc/a11y/FORK_VERSION>\n";
        return 2;
    }
    labelled_version_names_the_label();
    a_release_build_has_no_label();
    version_line_names_both_versions();
    shown_version_names_the_base_port_release();
    this_build_carries_the_version_file(argv[1]);
    std::cout << "fork_version: all tests passed\n";
    return 0;
}
