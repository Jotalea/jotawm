#!/usr/bin/env bash
# Build the jotawm .deb inside a throwaway Debian container.
#
# Usage:
#   ./scripts/debian-build.sh [--lint] [--image debian:stable] [--shell]
#
#   --lint    also install and run lintian against the built package
#   --image   Debian image to build in (default: debian:stable)
#   --shell   drop into an interactive shell in the build container instead
#             of building (useful for debugging a failed build)
#
# Output (.deb, .changes, .buildinfo, .dsc) is written to ./debian-build/
# in the repo root. The repo itself is mounted read-only; nothing is
# written back into it.

set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_DIR/debian-build"
IMAGE="debian:stable"
RUN_LINT=0
INTERACTIVE=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --lint) RUN_LINT=1; shift ;;
    --image) IMAGE="$2"; shift 2 ;;
    --shell) INTERACTIVE=1; shift ;;
    -h|--help)
      sed -n '2,20p' "${BASH_SOURCE[0]}"
      exit 0
      ;;
    *)
      echo "unknown argument: $1" >&2
      exit 1
      ;;
  esac
done

if ! command -v docker >/dev/null 2>&1; then
  echo "docker is not installed or not on PATH" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

BUILD_DEPS="build-essential devscripts debhelper pkg-config libx11-dev libxinerama-dev"
LINT_DEPS=""
LINT_CMD=""
if [[ "$RUN_LINT" -eq 1 ]]; then
  LINT_DEPS="lintian"
  LINT_CMD='lintian /out/*.changes || true'
fi

BUILD_SCRIPT=$(cat <<EOF
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends $BUILD_DEPS $LINT_DEPS

cp -a /src /tmp/jotawm
cd /tmp/jotawm
dpkg-buildpackage -us -uc -b

cp -v /tmp/jotawm_*.deb /tmp/jotawm_*.changes /tmp/jotawm_*.buildinfo /out/ 2>/dev/null || true

$LINT_CMD
EOF
)

if [[ "$INTERACTIVE" -eq 1 ]]; then
  echo "==> dropping into $IMAGE (repo mounted read-only at /src)"
  docker run --rm -it \
    -v "$REPO_DIR":/src:ro \
    -v "$OUT_DIR":/out \
    -w /src \
    "$IMAGE" bash
  exit $?
fi

echo "==> building jotawm .deb in $IMAGE"
docker run --rm \
  -v "$REPO_DIR":/src:ro \
  -v "$OUT_DIR":/out \
  "$IMAGE" bash -c "$BUILD_SCRIPT"

echo "==> done, artifacts in $OUT_DIR:"
ls -1 "$OUT_DIR"
