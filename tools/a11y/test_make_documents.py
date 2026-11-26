#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""make_documents.py's pandoc commands and its refusals, with pandoc faked.

The real pandoc runs in CI alone (.github/workflows/a11y.yml), where the
zips are made; here a fake stands in for it, writing what each case needs.
Run by ctest, or alone: python tools/a11y/test_make_documents.py
"""

import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_documents  # noqa: E402

failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


def fake_pandoc(contents, returncode=0):
    """A stand-in for subprocess.run that records each command and writes
    contents[output's name] to the command's -o file; a name missing from
    contents writes nothing."""
    calls = []

    def run(command, **_):
        calls.append(command)
        output = command[command.index("-o") + 1]
        name = os.path.basename(output)
        if name in contents:
            with open(output, "w", encoding="utf-8") as f:
                f.write(contents[name])
        return subprocess.CompletedProcess(command, returncode)

    return run, calls


def made(run):
    with tempfile.TemporaryDirectory() as out:
        return make_documents.make_documents("repo", out, run)


def test_commands():
    run, calls = fake_pandoc({"readme.html": "<html>", "changelog.html": "<html>"})
    errors = made(run)
    check(errors == [], f"two good files refused: {errors}")
    check(len(calls) == 2, f"{len(calls)} pandoc runs, not 2")
    for call, source, title in zip(calls, ("ACCESSIBILITY.md", "CHANGELOG.md"),
                                   ("Non-Visual Melee", "Non-Visual Melee changes")):
        check(call[:4] == ["pandoc", "-f", "gfm", "-s"], f"not gfm to a standalone page: {call}")
        check(f"pagetitle={title}" in call, f"{source}'s page not titled {title}: {call}")
        check("lang=en" in call, f"{source}'s page language not English: {call}")
        check(call[-1] == os.path.join("repo", source), f"not made from {source}: {call}")
        check("--toc" not in call, f"{source}'s page has a table of contents: {call}")


def test_refusals():
    run, _ = fake_pandoc({"readme.html": "<html>", "changelog.html": ""})
    errors = made(run)
    check(len(errors) == 1 and "changelog.html" in errors[0] and "empty" in errors[0],
          f"an empty changelog.html: {errors}")

    run, _ = fake_pandoc({"changelog.html": "<html>"})
    errors = made(run)
    check(len(errors) == 1 and "readme.html" in errors[0] and "missing" in errors[0],
          f"a missing readme.html: {errors}")

    run, _ = fake_pandoc({"readme.html": "<html>", "changelog.html": "<html>"}, returncode=64)
    errors = made(run)
    check(len(errors) == 2 and all("pandoc failed" in e for e in errors),
          f"pandoc failing: {errors}")

    def no_pandoc(command, **_):
        raise FileNotFoundError(command[0])

    errors = made(no_pandoc)
    check(len(errors) == 1 and "pandoc not found" in errors[0], f"no pandoc: {errors}")


def main():
    for test in (test_commands, test_refusals):
        test()
    for failure in failures:
        print(f"test_make_documents: {failure}")
    if not failures:
        print("test_make_documents: passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
