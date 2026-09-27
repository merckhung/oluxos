#!/usr/bin/env bash
# Regenerate docs/screenshots: one headless frame per boot stage (Vulkan via
# Mesa lavapipe works; no display needed).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel build //:bootviz
mkdir -p docs/screenshots
./bazel-bin/src/app/bootviz --screenshots="$PWD/docs/screenshots" "$@"
