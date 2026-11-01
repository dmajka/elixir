#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the cheat sheet (web/cheatsheet.html, the splash's potion put in at <!--POTION-->) printed to
# web/cheatsheet.pdf with headless Chrome. web/make_site.py puts it at the site's root; fmhub.json's "cheat" links
# to it there. Run it when a release adds or changes keys, pages or knobs (and commit the PDF).
#   tools/cheatsheet.sh        (after build/card_shot docs/card.png, if the card changed)
set -e
cd "$(dirname "$0")/.."
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
mkdir -p build
python3 -c '
import sys; sys.path.insert(0, "web"); import make_site
s = open("web/cheatsheet.html", encoding="utf-8").read().replace("<!--POTION-->", make_site.potion_svg(small=True))
open("build/cheatsheet.html", "w", encoding="utf-8").write(s)'
"$CHROME" --headless --disable-gpu --no-pdf-header-footer --allow-file-access-from-files \
    --print-to-pdf="$PWD/web/cheatsheet.pdf" "file://$PWD/build/cheatsheet.html" 2>/dev/null
echo "cheatsheet: web/cheatsheet.pdf ($(wc -c < web/cheatsheet.pdf | tr -d ' ') B)"
