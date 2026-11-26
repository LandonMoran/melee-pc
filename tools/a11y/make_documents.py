#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Make the zip's two documents, readme.html and changelog.html.

  python3 tools/a11y/make_documents.py OUT_DIR

Writes, into OUT_DIR (made if missing), each as a standalone HTML page in
English, made by pandoc from GitHub-flavoured Markdown:

  - readme.html from ACCESSIBILITY.md, titled "Non-Visual Melee";
  - changelog.html from CHANGELOG.md, titled "Non-Visual Melee changes".

Every zip CI makes carries both beside melee.exe or Melee.app
(.github/workflows/a11y.yml), each made from the commit it builds. Needs
pandoc on PATH. Exits 1, naming the file, when pandoc fails or leaves a page
missing or empty, so the job fails rather than ship a zip without it.
"""

import argparse
import os
import subprocess
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# (source, page, title). The title is the page's <title> alone: pandoc puts
# a "title" in the page as an extra h1 above the document's own, which
# already opens with the same words, so the page would start with two
# identical headings; "pagetitle" sets the browser's title and nothing more.
DOCUMENTS = (
    ("ACCESSIBILITY.md", "readme.html", "Non-Visual Melee"),
    ("CHANGELOG.md", "changelog.html", "Non-Visual Melee changes"),
)


def pandoc_command(source, page, title):
    """gfm in, a standalone page out (-s: pandoc's own template with its
    short style for a readable line width, and no table of contents)."""
    return ["pandoc", "-f", "gfm", "-s",
            "--metadata", f"pagetitle={title}", "--metadata", "lang=en",
            "-o", page, source]


def make_documents(repo_root, out_dir, run=subprocess.run):
    """Make each page into out_dir; return an error line per page that
    failed, empty when both were made."""
    errors = []
    for source, name, title in DOCUMENTS:
        page = os.path.join(out_dir, name)
        try:
            result = run(pandoc_command(os.path.join(repo_root, source), page, title))
        except FileNotFoundError:
            return ["pandoc not found on PATH"]
        if result.returncode != 0:
            errors.append(f"pandoc failed ({result.returncode}) making {name} from {source}")
        elif not os.path.isfile(page):
            errors.append(f"{name} missing after pandoc")
        elif os.path.getsize(page) == 0:
            errors.append(f"{name} empty after pandoc")
    return errors


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("out_dir", metavar="OUT_DIR", help="the folder the pages go into")
    args = parser.parse_args()
    os.makedirs(args.out_dir, exist_ok=True)
    errors = make_documents(REPO_ROOT, args.out_dir)
    for error in errors:
        print(f"make_documents: {error}", file=sys.stderr)
    if errors:
        sys.exit(1)
    for _, name, _ in DOCUMENTS:
        page = os.path.join(args.out_dir, name)
        print(f"make_documents: {page}, {os.path.getsize(page)} bytes")


if __name__ == "__main__":
    main()
