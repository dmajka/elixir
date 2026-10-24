#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Kettle: the site (web/make_site.py) committed to the gh-pages branch, for GitHub Pages (Settings > Pages > Deploy
# from a branch: gh-pages, / (root)). It commits only; push it yourself: git push origin gh-pages
#   tools/publish_site.sh        (after ./build.sh and web/emu/build.sh)
set -e
cd "$(dirname "$0")/.."
python3 web/make_site.py
VER=$(sed -n 's/^#define KETTLE_VERSION "\([^"]*\)".*/\1/p' firmware/src/kettle.c | head -n 1)
WT=build/gh-pages
git worktree remove --force "$WT" 2>/dev/null || true
if git show-ref --quiet refs/heads/gh-pages; then
    git worktree add -q "$WT" gh-pages
else
    git worktree add -q --detach "$WT"
    (cd "$WT" && git checkout -q --orphan gh-pages && git rm -rq --cached . && git clean -fdxq)
fi
find "$WT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
cp -R build/site/. "$WT"/
cd "$WT"
git add -A
if git diff --cached --quiet; then
    echo "publish_site: no changes"
else
    git commit -q -m "Site: Kettle $VER"
    echo "publish_site: committed Kettle $VER to gh-pages (git push origin gh-pages)"
fi
cd - >/dev/null
git worktree remove "$WT"
