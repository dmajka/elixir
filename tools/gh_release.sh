#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Elixir: the GitHub release for the version in firmware/src/elixir.c. GitHub builds and signs it: pushing the vX.Y
# tag runs .github/workflows/release.yml (The Firmware Hub's shared fm1-build workflow: the package built from the tag
# on GitHub's Ubuntu runner, the tests, a Sigstore attestation, the release with the package). This waits for that
# run, titles the release and gives it the tag's message as notes, verifies the attestation, and checks the signed
# package is the site's byte for byte (the builds are reproducible: the same toolchain gives the same package).
# Never upload or replace the package by hand: the hub's "Built from source" needs the attested file.
#   tools/gh_release.sh        (after tools/publish_site.sh and git push origin main gh-pages vX.Y)
set -e
cd "$(dirname "$0")/.."
VER=$(sed -n 's/^#define ELIXIR_VERSION "\([^"]*\)".*/\1/p' firmware/src/elixir.c | head -n 1)
case "$VER" in *-*) echo "gh_release: $VER is a test build: releases are for vX.Y only"; exit 1;; esac
X=${VER#v}
REPO=dmajka/elixir
SITE=build/site/firmware/elixir-$VER.fwsc
git ls-remote --exit-code --tags origin "$VER" >/dev/null || { echo "gh_release: tag $VER isn't on GitHub (git push origin $VER)"; exit 1; }

RUN=
for i in $(seq 1 36); do                             # (the run shows up a few seconds after the tag)
    RUN=$(gh run list -R "$REPO" --workflow release.yml --branch "$VER" --limit 1 --json databaseId --jq '.[0].databaseId')
    [ -n "$RUN" ] && break
    sleep 5
done
[ -n "$RUN" ] || { echo "gh_release: no release run for $VER (Actions tab)"; exit 1; }
echo "gh_release: waiting for run $RUN (build, tests, signing)"
gh run watch -R "$REPO" "$RUN" --exit-status >/dev/null

mkdir -p build/signed
git tag -l --format='%(contents:body)' "$VER" > build/signed/notes.md
gh release edit -R "$REPO" "$VER" --title "Elixir $X" --notes-file build/signed/notes.md >/dev/null
gh release download -R "$REPO" "$VER" -p "elixir-$X.fwsc" -D build/signed --clobber
gh attestation verify "build/signed/elixir-$X.fwsc" -R "$REPO" \
    --signer-workflow zvenson/fm1-build/.github/workflows/build.yml >/dev/null
echo "gh_release: $VER released, elixir-$X.fwsc signed (attestation verified)"
if [ -f "$SITE" ] && cmp -s "build/signed/elixir-$X.fwsc" "$SITE"; then
    echo "gh_release: the site's package is the signed one, byte for byte"
else
    echo "gh_release: WARNING: the site's $SITE differs from the signed package (a different toolchain?):"
    echo "  python3 web/make_site.py build/signed/elixir-$X.fwsc, then publish the site again"
    exit 1
fi
