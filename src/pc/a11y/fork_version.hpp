/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include <string>

namespace a11y {

/* The two versions a fork build carries, and the label that tells builds of
 * one version apart (docs/adr/0011-fork-releases-are-versioned-apart-from-
 * the-base-port.md). */
struct BuildVersions {
    /* The fork's own version, src/pc/a11y/FORK_VERSION as written: "0.1.0", the
     * latest release's. Players, the changelog and the update check go by
     * it; the one version compared with a release's. */
    std::string fork;
    /* "local build", "dev build 1a2b3c4" on a CI build, empty on a release's
     * tag build. Shown and logged beside the fork version, never compared. */
    std::string label;
    /* The base port's app version, the root VERSION as written: "v0.4.2".
     * Online pairing goes by it, so the fork never changes it. */
    std::string app;
};

/* This build's, as a11y.cmake compiled them in. */
const BuildVersions& build_versions();

/* The fork version with its label in brackets, or alone when there is no
 * label: "0.1.0 (local build)". */
std::string labelled_fork_version(const BuildVersions& versions);

/* The version the launcher's title bar and the port menu's show where the
 * base port shows its app version: the fork version with the base port's
 * beside it, and the label, when there is one, first in the brackets:
 * "0.1.0 (Melee-PC v0.4.2)", "0.1.0 (local build, Melee-PC v0.4.2)". */
std::string shown_version(const BuildVersions& versions);

/* The startup log line: "Non-Visual Melee 0.1.0 (local build), Melee-PC
 * v0.4.2". */
std::string version_line(const BuildVersions& versions);

}  // namespace a11y
