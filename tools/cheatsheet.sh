#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the cheat sheet (web/cheatsheet.html) printed to web/cheatsheet.pdf with headless Chrome. web/make_site.py
# puts it at the site's root; fmhub.json's "cheat" links to it there.
#   tools/cheatsheet.sh        (after build/card_shot docs/card.png, if the card changed)
set -e
cd "$(dirname "$0")/.."
CHROME=${CHROME:-"/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"}
"$CHROME" --headless --disable-gpu --no-pdf-header-footer --allow-file-access-from-files \
    --print-to-pdf="$PWD/web/cheatsheet.pdf" "file://$PWD/web/cheatsheet.html" 2>/dev/null
echo "cheatsheet: web/cheatsheet.pdf ($(wc -c < web/cheatsheet.pdf | tr -d ' ') B)"
