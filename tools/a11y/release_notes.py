#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check a release tag and write its release notes.

  python tools/a11y/release_notes.py TAG [--commit REV] [--branch REF]
      [--repo OWNER/NAME] [--changelog FILE] [--notes FILE] [--outputs FILE]

The tag build of .github/workflows/a11y.yml runs this before any build and
again just before it drafts the release; locally it runs the same, so a
release can be tried before its tag is pushed. It checks, and refuses with
one line per failed check:

  tag        TAG is a11y-v plus src/pc/a11y/FORK_VERSION, a MAJOR.MINOR.PATCH.
  changelog  CHANGELOG.md's first version section is that version's,
             "## <version> (<YYYY-MM-DD>)", with only an empty Unreleased
             above it; it opens with "Plays online with Melee-PC <base>.",
             <base> being the root VERSION file's, and has changes below.
  master     REV (default HEAD) is on REF (default origin/master):
             git merge-base --is-ancestor.
  release    the repository has no release for TAG yet, draft or published:
             gh release view, which needs gh signed in (GH_TOKEN in CI) with
             push access to see drafts.

When every check passes it writes the notes (to FILE, or to standard output)
in this order: what Non-Visual Melee is, for a reader given the release's
link, and a link to ACCESSIBILITY.md at the tag; under "## Download", a link
to the release's Windows zip, the requirements and "Online, it plays against
Non-Visual Melee <version> and Melee-PC <base>"; under "## Changes", the
version's section of CHANGELOG.md without its heading or its "Plays online"
line; under "## Full changelog", a link to CHANGELOG.md at the tag. --outputs appends "version=<version>" and "title=<title>" to FILE,
as GitHub's $GITHUB_OUTPUT takes them; the title is "Non-Visual Melee
<version> beta" while the version is 0.x.

Exits 1 on any failed check, writing no notes.
"""

import argparse
import json
import os
import re
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_REPO = "ohylli/non-visual-melee"
TAG_PREFIX = "a11y-v"
FORK_VERSION_FILE = "src/pc/a11y/FORK_VERSION"
VERSION_RE = re.compile(r"^(\d+)\.(\d+)\.(\d+)$")
HEADING_RE = re.compile(r"^## (.*?)\s*$")
VERSION_HEADING_RE = re.compile(r"^(\d+\.\d+\.\d+) \((\d{4}-\d{2}-\d{2})\)$")

# The release's one asset: a11y.yml's ZIP_NAME. The notes link it directly,
# since GitHub's Assets list can be collapsed and is hard to find by ear.
ZIP_NAME = "NonVisualMelee-Windows-x86_64.zip"

INTRODUCTION = (
    "Non-Visual Melee makes Super Smash Bros. Melee playable by blind players: your "
    "screen reader reads the menus, and voices and audio cues guide you through matches."
)

REQUIREMENTS = (
    "It needs Windows 10 or 11 (64-bit), a screen reader such as NVDA, and your own "
    "disc image of Super Smash Bros. Melee (USA, revision 2); no game data comes with it."
)


class Refusal(Exception):
    """A failed check, its message the line that says why."""


def first_line(path):
    try:
        with open(path, encoding="utf-8") as f:
            return f.readline().strip()
    except OSError as error:
        raise Refusal(f"cannot read {path}: {error.strerror}") from None


def check_tag(tag, fork):
    if not VERSION_RE.match(fork):
        raise Refusal(f"{FORK_VERSION_FILE} holds \"{fork}\", not a MAJOR.MINOR.PATCH "
                      "such as 0.1.0")
    if tag != TAG_PREFIX + fork:
        raise Refusal(f"the tag is {tag}, but {FORK_VERSION_FILE} holds {fork}, "
                      f"so the tag must be {TAG_PREFIX}{fork}")


def sections(text):
    """CHANGELOG.md's "## " sections in order, as (heading, body lines)."""
    found = []
    for line in text.splitlines():
        heading = HEADING_RE.match(line)
        if heading:
            found.append((heading.group(1), []))
        elif found:
            found[-1][1].append(line)
    return found


def strip_blank(lines):
    while lines and not lines[0].strip():
        lines = lines[1:]
    while lines and not lines[-1].strip():
        lines = lines[:-1]
    return lines


def changelog_changes(path, fork, base):
    """The version's changes: its section's body past the "Plays online"
    line, as text. Refuses unless the section is the first and well formed."""
    try:
        with open(path, encoding="utf-8") as f:
            text = f.read()
    except OSError as error:
        raise Refusal(f"cannot read {path}: {error.strerror}") from None
    found = sections(text)
    if found and found[0][0] == "Unreleased":
        if strip_blank(found[0][1]):
            raise Refusal("CHANGELOG.md's Unreleased section has entries: at a release it "
                          f"becomes the \"## {fork} (<date>)\" section")
        found = found[1:]
    if not found:
        raise Refusal(f"CHANGELOG.md has no version section; the first must be {fork}'s")
    heading, body = found[0]
    version = VERSION_HEADING_RE.match(heading)
    if not version:
        raise Refusal(f"CHANGELOG.md's first version section is \"## {heading}\", not "
                      f"\"## {fork} (<date>)\" with the date as YYYY-MM-DD")
    if version.group(1) != fork:
        raise Refusal(f"CHANGELOG.md's first version section is {version.group(1)}'s, "
                      f"not {fork}'s")
    body = strip_blank(body)
    pairing = f"Plays online with Melee-PC {base}."
    if not body or body[0].strip() != pairing:
        opening = body[0].strip() if body else "nothing"
        raise Refusal(f"CHANGELOG.md's {fork} section opens with \"{opening}\", not "
                      f"\"{pairing}\" (the root VERSION holds {base})")
    changes = strip_blank(body[1:])
    if not changes:
        raise Refusal(f"CHANGELOG.md's {fork} section has no changes below its "
                      "\"Plays online\" line")
    return "\n".join(changes)


def check_master(root, commit, branch, run):
    try:
        result = run(["git", "-C", root, "merge-base", "--is-ancestor", commit, branch],
                     capture_output=True, text=True)
    except FileNotFoundError:
        raise Refusal("git not found on PATH") from None
    if result.returncode == 1:
        raise Refusal(f"{commit} is not on {branch}: tag a commit master holds")
    if result.returncode != 0:
        raise Refusal(f"cannot tell whether {commit} is on {branch}: "
                      f"{result.stderr.strip()}")


def check_release(tag, repo, run):
    try:
        result = run(["gh", "release", "view", tag, "--repo", repo, "--json", "isDraft"],
                     capture_output=True, text=True)
    except FileNotFoundError:
        raise Refusal("gh not found on PATH") from None
    if result.returncode == 0:
        try:
            draft = json.loads(result.stdout).get("isDraft", False)
        except ValueError:
            draft = False
        kind = "a draft" if draft else "a published"
        raise Refusal(f"{repo} already has {kind} release for {tag}, which a run never "
                      "replaces: delete a failed draft by hand before a re-run, and fix a "
                      "published release with the next patch")
    if "release not found" not in result.stderr:
        raise Refusal(f"cannot ask GitHub whether {tag} has a release: "
                      f"{result.stderr.strip()}")


def release_title(fork):
    beta = " beta" if fork.split(".")[0] == "0" else ""
    return f"Non-Visual Melee {fork}{beta}"


def release_notes(fork, base, changes, tag, repo):
    at_tag = f"https://github.com/{repo}/blob/{tag}"
    download = f"https://github.com/{repo}/releases/download/{tag}/{ZIP_NAME}"
    return (
        f"{INTRODUCTION}\n"
        "\n"
        f"The [Non-Visual Melee guide]({at_tag}/ACCESSIBILITY.md) says what is accessible "
        "and how to get started. The download includes it as readme.html.\n"
        "\n"
        "## Download\n"
        "\n"
        f"[Download Non-Visual Melee {fork} for Windows]({download}) (a zip file).\n"
        "\n"
        f"{REQUIREMENTS}\n"
        "\n"
        f"Online, it plays against Non-Visual Melee {fork} and Melee-PC {base}, the PC "
        "version of Melee that Non-Visual Melee is built on.\n"
        "\n"
        "## Changes\n"
        "\n"
        f"{changes}\n"
        "\n"
        "## Full changelog\n"
        "\n"
        f"[CHANGELOG.md]({at_tag}/CHANGELOG.md) has the changes of every release. The "
        "download includes it as changelog.html.\n"
    )


def main(argv=None, run=subprocess.run, out=sys.stdout, err=sys.stderr):
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("tag", metavar="TAG", help="the release's tag, such as a11y-v0.1.0")
    parser.add_argument("--commit", default="HEAD", metavar="REV",
                        help="the tagged commit (default HEAD)")
    parser.add_argument("--branch", default="origin/master", metavar="REF",
                        help="the branch the commit must be on (default origin/master)")
    parser.add_argument("--repo", default=DEFAULT_REPO, metavar="OWNER/NAME",
                        help=f"the repository of the release and the links (default "
                             f"{DEFAULT_REPO})")
    parser.add_argument("--changelog", metavar="FILE",
                        help="read this changelog in place of the root's CHANGELOG.md, "
                             "to try the notes on a test section")
    parser.add_argument("--notes", metavar="FILE", help="write the notes here, not to "
                                                        "standard output")
    parser.add_argument("--outputs", metavar="FILE",
                        help="append version= and title= lines here ($GITHUB_OUTPUT)")
    parser.add_argument("--root", default=REPO_ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)

    changelog = args.changelog or os.path.join(args.root, "CHANGELOG.md")
    refusals = []
    fork = base = changes = None
    try:
        fork = first_line(os.path.join(args.root, FORK_VERSION_FILE))
        check_tag(args.tag, fork)
    except Refusal as refusal:
        refusals.append(("tag", refusal))
    try:
        base = first_line(os.path.join(args.root, "VERSION"))
        if fork is not None:
            changes = changelog_changes(changelog, fork, base)
    except Refusal as refusal:
        refusals.append(("changelog", refusal))
    for name, check in (("master", lambda: check_master(args.root, args.commit, args.branch,
                                                        run)),
                        ("release", lambda: check_release(args.tag, args.repo, run))):
        try:
            check()
        except Refusal as refusal:
            refusals.append((name, refusal))

    for name, refusal in refusals:
        print(f"release_notes: refused, {name}: {refusal}", file=err)
    if refusals:
        return 1

    notes = release_notes(fork, base, changes, args.tag, args.repo)
    if args.notes:
        with open(args.notes, "w", encoding="utf-8", newline="\n") as f:
            f.write(notes)
    else:
        out.write(notes)
    if args.outputs:
        with open(args.outputs, "a", encoding="utf-8", newline="\n") as f:
            f.write(f"version={fork}\ntitle={release_title(fork)}\n")
    print(f"release_notes: {args.tag} passed every check: \"{release_title(fork)}\"", file=err)
    return 0


if __name__ == "__main__":
    sys.exit(main())
