#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the GitHub release for the version in firmware/src/elixir.c, with the site's package (build/site/firmware/
# elixir-vX.Y.fwsc, made by tools/publish_site.sh) attached: The Firmware Hub picks up each new release from it.
# The tag must be pushed first; the notes are the tag's message. Needs the GitHub CLI, logged in (gh auth login).
#   tools/gh_release.sh        (after tools/publish_site.sh and git push origin main gh-pages vX.Y)
set -e
cd "$(dirname "$0")/.."
VER=$(sed -n 's/^#define ELIXIR_VERSION "\([^"]*\)".*/\1/p' firmware/src/elixir.c | head -n 1)
case "$VER" in *-*) echo "gh_release: $VER is a test build: releases are for vX.Y only"; exit 1;; esac
PKG=build/site/firmware/elixir-$VER.fwsc
[ -f "$PKG" ] || { echo "gh_release: no $PKG (run tools/publish_site.sh first)"; exit 1; }
git ls-remote --exit-code --tags origin "$VER" >/dev/null || { echo "gh_release: tag $VER isn't on GitHub (git push origin $VER)"; exit 1; }
if gh release view "$VER" >/dev/null 2>&1; then
    gh release upload "$VER" "$PKG" --clobber
    echo "gh_release: $VER existed: its package replaced"
else
    gh release create "$VER" "$PKG" --verify-tag --title "Elixir ${VER#v}" --notes-from-tag
    echo "gh_release: $VER released with $(basename "$PKG")"
fi
