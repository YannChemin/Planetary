#!/usr/bin/env python3
"""
Convert Planetary GRASS addon Markdown manuals to HTML body fragments
compatible with the GRASS mkhtml.py build system.

Usage:
    python3 scripts/md2html_grass.py planetary/   # convert all *.md → *.html
    python3 scripts/md2html_grass.py p.landing.md # convert one file

The GRASS Html.make rule:
    $(HTMLDIR)/%.html: %.html %.tmp.html $(HTMLSRC) ...
requires a local %.html body file in each module directory.  Modules that
only have %.md would have that prerequisite missing and HTML pages would
never be generated.  This script produces the HTML body from the *.md
source so make can proceed.

Output format follows the GRASS convention (no <html>/<head>/<body>
wrapper; code blocks use <div class="code"><pre>; links to .md files
are rewritten to .html).
"""

import os
import re
import sys

try:
    import mistune
except ImportError:
    sys.exit(
        "error: python3-mistune is required (apt-get install python3-mistune)"
    )

# ── mistune renderer ──────────────────────────────────────────────────────────

class GrassRenderer(mistune.HTMLRenderer):
    """Render Markdown to GRASS HTML body conventions."""

    def codespan(self, code):
        return f"<code>{mistune.escape(code)}</code>"

    def block_code(self, code, **kwargs):
        # GRASS manual pages use <div class="code"><pre> for all code blocks
        return f'<div class="code"><pre>\n{mistune.escape(code)}</pre></div>\n'

    def link(self, text, url, title=None):
        # Rewrite cross-module .md links → .html for local manual pages
        url = re.sub(r'\.md$', '.html', url)
        if title:
            return f'<a href="{url}" title="{title}">{text}</a>'
        return f'<a href="{url}">{text}</a>'

    def heading(self, text, level, **kwargs):
        return f"<h{level}>{text}</h{level}>\n"

    def paragraph(self, text):
        return f"<p>\n{text}\n</p>\n"

    def table(self, text):
        return f'<table border="1">\n{text}</table>\n'


_renderer = GrassRenderer(escape=False)
_md = mistune.create_markdown(
    renderer=_renderer,
    plugins=["table", "strikethrough"],
    escape=False,
)


def convert_md_to_html(src_path: str, dst_path: str) -> None:
    with open(src_path, encoding="utf-8") as f:
        src = f.read()
    html = _md(src)
    with open(dst_path, "w", encoding="utf-8") as f:
        f.write(html)
    print(f"  {src_path} → {dst_path}")


def convert_directory(root: str) -> int:
    """Scan root for module *.md files and convert each to *.html."""
    count = 0
    for dirpath, _dirs, files in os.walk(root):
        for fname in files:
            if not fname.endswith(".md"):
                continue
            stem = fname[:-3]
            # Only convert module manuals: name must match the directory name
            if os.path.basename(dirpath) != stem:
                continue
            src = os.path.join(dirpath, fname)
            dst = os.path.join(dirpath, stem + ".html")
            # Regenerate only when .md is newer than .html
            if (
                os.path.exists(dst)
                and os.path.getmtime(dst) >= os.path.getmtime(src)
            ):
                continue
            convert_md_to_html(src, dst)
            count += 1
    return count


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    target = sys.argv[1]

    if os.path.isdir(target):
        n = convert_directory(target)
        print(f"md2html_grass: {n} file(s) converted under {target}")
    elif os.path.isfile(target) and target.endswith(".md"):
        dst = target[:-3] + ".html"
        convert_md_to_html(target, dst)
    else:
        sys.exit(f"error: expected a directory or a *.md file, got: {target}")


if __name__ == "__main__":
    main()
