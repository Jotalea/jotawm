#!/usr/bin/env bash
# Regenerate debian/changelog's entry from git metadata so the Debian
# package version always tracks HEAD, mirroring the pkgver() logic in
# PKGBUILD. Run this right before dpkg-buildpackage -- dpkg-buildpackage
# parses debian/changelog before debian/rules ever runs, so it can't be
# done from within the rules file itself.
#
# This mutates the working tree only; the regenerated entry is not meant
# to be committed (same as how the "aur" CI job sed's PKGBUILD's pkgver
# in its own checkout without pushing that change back to this repo).

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_DIR"

VERSION="$(git describe --long --tags 2>/dev/null | sed 's/\([^-]*-g\)/r\1/;s/-/./g')"
if [ -z "$VERSION" ]; then
  VERSION="$(date +%Y.%m.%d).r$(git rev-list --count HEAD).$(git rev-parse --short HEAD)"
fi

SUBJECT="$(git log -1 --pretty=%s)"

cat > debian/changelog <<CHANGELOG
jotawm (${VERSION}-1) unstable; urgency=medium

  * ${SUBJECT}

 -- Jotalea <main@jotalea.com.ar>  $(date -R)
CHANGELOG
