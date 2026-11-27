#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""release_notes.py's checks and notes, through its arguments, with git and
gh faked.

Each case writes a tree of its own (src/pc/a11y/FORK_VERSION, VERSION and
CHANGELOG.md) into a scratch folder and runs the script's main on it with
--root; a fake stands in for git and gh, answering as each case needs. The
real git and gh answer in CI's tag build (.github/workflows/a11y.yml).
Run by ctest, or alone: python tools/a11y/test_release_notes.py
"""

import io
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import release_notes  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


GOOD_CHANGELOG = """# Non-Visual Melee changes

What changed in each release of Non-Visual Melee, newest first.

## Unreleased

## 0.2.0 (2026-12-01)

Plays online with Melee-PC v0.4.2.

### New

- Stage select reads the stage's size.

### Fixed

- The ledge warning no longer pulses in the air.

## 0.1.0 (2026-11-24)

Plays online with Melee-PC v0.4.1.

### New

- The first beta.
"""


def fake_tools(is_ancestor=(0, ""), release=(1, "release not found\n", "")):
    """A stand-in for subprocess.run recording each command. git's
    merge-base answers is_ancestor (returncode, stderr); gh release view
    answers release (returncode, stderr, stdout)."""
    calls = []

    def run(command, **_):
        calls.append(command)
        if command[0] == "git":
            code, stderr = is_ancestor
            return subprocess.CompletedProcess(command, code, "", stderr)
        if command[0] == "gh":
            code, stderr, stdout = release
            return subprocess.CompletedProcess(command, code, stdout, stderr)
        raise AssertionError(f"unexpected command {command}")

    return run, calls


class Result:
    def __init__(self, code, out, err, notes, outputs, calls):
        self.code, self.out, self.err = code, out, err
        self.notes, self.outputs, self.calls = notes, outputs, calls


def run_script(args, fork="0.2.0", base="v0.4.2", changelog=GOOD_CHANGELOG, run=None,
               write_notes=True):
    """Run main on a scratch tree; args are the script's arguments past
    --root, with "{dir}" standing for the scratch folder."""
    if run is None:
        run, _ = fake_tools()
    with tempfile.TemporaryDirectory() as root:
        os.makedirs(os.path.join(root, "src", "pc", "a11y"))
        for path, text in (("src/pc/a11y/FORK_VERSION", fork + "\n"),
                           ("VERSION", base + "\n"),
                           ("CHANGELOG.md", changelog)):
            with open(os.path.join(root, path), "w", encoding="utf-8", newline="\n") as f:
                f.write(text)
        notes_path = os.path.join(root, "notes.md")
        outputs_path = os.path.join(root, "outputs.txt")
        argv = ["--root", root] + [a.replace("{dir}", root) for a in args]
        if write_notes:
            argv += ["--notes", notes_path, "--outputs", outputs_path]
        out, err = io.StringIO(), io.StringIO()
        code = release_notes.main(argv, run=run, out=out, err=err)
        notes = outputs = None
        if os.path.exists(notes_path):
            with open(notes_path, encoding="utf-8") as f:
                notes = f.read()
        if os.path.exists(outputs_path):
            with open(outputs_path, encoding="utf-8") as f:
                outputs = f.read()
        calls = getattr(run, "calls", None)
        return Result(code, out.getvalue(), err.getvalue(), notes, outputs, calls)


def refused(result, check_name, words, case):
    """The run exited 1, wrote no notes, and named the check and the words."""
    check(result.code == 1, f"{case}: exit {result.code}, not 1")
    check(result.notes is None, f"{case}: notes written anyway")
    lines = [line for line in result.err.splitlines() if f"refused, {check_name}:" in line]
    check(lines, f"{case}: no '{check_name}' refusal in: {result.err!r}")
    for word in words:
        check(any(word in line for line in lines), f"{case}: '{word}' not in {lines}")


def test_good_release():
    run, calls = fake_tools()
    result = run_script(["a11y-v0.2.0", "--commit", "abc1234"], run=run)
    check(result.code == 0, f"a good release refused: {result.err}")
    notes = result.notes or ""
    url = "https://github.com/ohylli/non-visual-melee/blob/a11y-v0.2.0"
    download = ("https://github.com/ohylli/non-visual-melee/releases/download/a11y-v0.2.0/"
                "NonVisualMelee-Windows-x86_64.zip")
    check(notes.startswith("Non-Visual Melee makes Super Smash Bros. Melee playable"),
          f"notes do not open with the introduction: {notes[:120]!r}")
    check(f"({url}/ACCESSIBILITY.md)" in notes, "no link to ACCESSIBILITY.md at the tag")
    check(f"({url}/CHANGELOG.md)" in notes, "no link to CHANGELOG.md at the tag")
    check(f"[Download Non-Visual Melee 0.2.0 for Windows]({download})" in notes,
          "no download link to the release's zip")
    check("Windows 10" in notes and "screen reader" in notes and "disc image" in notes,
          "the requirements missing")
    order = [notes.find(s) for s in ("ACCESSIBILITY.md", "## Download", "[Download",
                                     "Windows 10",
                                     "plays against Non-Visual Melee 0.2.0 and Melee-PC v0.4.2",
                                     "## Changes",
                                     "### New", "- Stage select", "### Fixed",
                                     "- The ledge warning", "## Full changelog",
                                     "CHANGELOG.md")]
    check(all(i >= 0 for i in order) and order == sorted(order),
          f"notes' parts missing or out of order: {order}\n{notes}")
    check("Plays online with Melee-PC" not in notes, "the section's own pairing line repeated")
    check("## 0.2.0" not in notes, "the section's heading kept")
    check("The first beta" not in notes and "0.1.0" not in notes, "the older section leaked in")
    check(result.outputs == "version=0.2.0\ntitle=Non-Visual Melee 0.2.0 beta\n",
          f"outputs: {result.outputs!r}")
    check(["git", "-C"] == calls[0][:2] and calls[0][3:] == ["merge-base", "--is-ancestor",
                                                             "abc1234", "origin/master"],
          f"master check: {calls[0]}")
    check(calls[1][:4] == ["gh", "release", "view", "a11y-v0.2.0"]
          and calls[1][calls[1].index("--repo") + 1] == "ohylli/non-visual-melee",
          f"release check: {calls[1]}")


def test_notes_on_stdout_and_options():
    run, calls = fake_tools()
    with tempfile.TemporaryDirectory() as other:
        changelog = os.path.join(other, "CHANGELOG.md")
        with open(changelog, "w", encoding="utf-8") as f:
            f.write(GOOD_CHANGELOG.replace("reads the stage's size", "from another file"))
        result = run_script(["a11y-v0.2.0", "--changelog", changelog, "--branch", "upstream/x",
                             "--repo", "someone/fork"], run=run, write_notes=False)
    check(result.code == 0, f"--changelog refused: {result.err}")
    check("from another file" in result.out, f"--changelog not read, or notes not on stdout: "
                                              f"{result.out!r}")
    check("https://github.com/someone/fork/blob/a11y-v0.2.0/CHANGELOG.md" in result.out,
          "--repo not in the links")
    check(calls[0][-2:] == ["HEAD", "upstream/x"], f"--commit default or --branch: {calls[0]}")
    check(calls[1][calls[1].index("--repo") + 1] == "someone/fork", f"--repo: {calls[1]}")


def test_title():
    good = GOOD_CHANGELOG.replace("0.2.0", "1.0.0")
    result = run_script(["a11y-v1.0.0"], fork="1.0.0", changelog=good)
    check(result.outputs == "version=1.0.0\ntitle=Non-Visual Melee 1.0.0\n",
          f"a 1.x release still called beta: {result.outputs!r}")


def test_tag():
    refused(run_script(["a11y-v0.2.1"]), "tag", ["a11y-v0.2.1", "a11y-v0.2.0"],
            "a tag for another version")
    refused(run_script(["v0.2.0"]), "tag", ["v0.2.0"], "a tag without a11y-")
    refused(run_script(["a11y-v0.2"], fork="0.2"), "tag", ["FORK_VERSION", "0.2"],
            "a malformed fork version")


def test_changelog():
    older_first = GOOD_CHANGELOG.replace("## 0.2.0 (2026-12-01)", "## 0.1.1 (2026-12-01)")
    refused(run_script(["a11y-v0.2.0"], changelog=older_first), "changelog",
            ["0.1.1", "0.2.0"], "another version's section first")

    pending = GOOD_CHANGELOG.replace("## Unreleased\n", "## Unreleased\n\n- Not released yet.\n")
    refused(run_script(["a11y-v0.2.0"], changelog=pending), "changelog", ["Unreleased"],
            "an Unreleased section with entries")

    only_unreleased = "# Changes\n\n## Unreleased\n\n### New\n\n- Something.\n"
    refused(run_script(["a11y-v0.2.0"], changelog=only_unreleased), "changelog",
            ["Unreleased"], "no version section at all")

    no_date = GOOD_CHANGELOG.replace("## 0.2.0 (2026-12-01)", "## 0.2.0")
    refused(run_script(["a11y-v0.2.0"], changelog=no_date), "changelog", ["0.2.0", "date"],
            "a heading without its date")

    no_pairing = GOOD_CHANGELOG.replace("Plays online with Melee-PC v0.4.2.\n\n", "", 1)
    refused(run_script(["a11y-v0.2.0"], changelog=no_pairing), "changelog",
            ["Plays online with Melee-PC v0.4.2."], "no pairing line")

    refused(run_script(["a11y-v0.2.0"], base="v0.4.3"), "changelog",
            ["v0.4.2", "v0.4.3"], "a pairing line naming another base version")

    empty = GOOD_CHANGELOG.split("### New")[0] + "## 0.1.0 (2026-11-24)\n\n- Old.\n"
    refused(run_script(["a11y-v0.2.0"], changelog=empty), "changelog", ["no changes"],
            "a section with no changes")

    refused(run_script(["a11y-v0.2.0", "--changelog", "{dir}/missing.md"]), "changelog",
            ["missing.md"], "a missing changelog")


def test_master():
    run, _ = fake_tools(is_ancestor=(1, ""))
    refused(run_script(["a11y-v0.2.0", "--commit", "abc1234"], run=run), "master",
            ["abc1234 is not on origin/master"], "a commit off master")

    run, _ = fake_tools(is_ancestor=(128, "fatal: Not a valid object name origin/master\n"))
    refused(run_script(["a11y-v0.2.0"], run=run), "master", ["Not a valid object name"],
            "no origin/master to ask")


def test_release():
    run, _ = fake_tools(release=(0, "", '{"isDraft":true,"tagName":"a11y-v0.2.0"}'))
    refused(run_script(["a11y-v0.2.0"], run=run), "release", ["a11y-v0.2.0", "draft"],
            "a draft release already there")

    run, _ = fake_tools(release=(0, "", '{"isDraft":false,"tagName":"a11y-v0.2.0"}'))
    refused(run_script(["a11y-v0.2.0"], run=run), "release", ["a11y-v0.2.0", "published"],
            "a published release already there")

    run, _ = fake_tools(release=(1, "HTTP 401: Bad credentials\n", ""))
    refused(run_script(["a11y-v0.2.0"], run=run), "release", ["Bad credentials"],
            "GitHub not answering")

    def no_gh(command, **_):
        if command[0] == "gh":
            raise FileNotFoundError("gh")
        return subprocess.CompletedProcess(command, 0, "", "")

    refused(run_script(["a11y-v0.2.0"], run=no_gh), "release", ["gh not found"], "no gh")


def test_every_failure_named():
    run, _ = fake_tools(is_ancestor=(1, ""),
                        release=(0, "", '{"isDraft":false,"tagName":"a11y-v0.2.1"}'))
    result = run_script(["a11y-v0.2.1"], run=run)
    for name in ("tag", "master", "release"):
        refused(result, name, [], f"three mismatches at once, {name}")


def main():
    for test in (test_good_release, test_notes_on_stdout_and_options, test_title, test_tag,
                 test_changelog, test_master, test_release, test_every_failure_named):
        test()
    for failure in failures:
        print(f"test_release_notes: {failure}")
    if not failures:
        print("test_release_notes: passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
