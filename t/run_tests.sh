#!/bin/bash
set -euo pipefail

if (( EUID == 0 )); then
  echo "Run this script as your normal user, without sudo." >&2
  exit 1
fi

# Find repo root relative to the script location
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
REPO_ROOT="$( cd "${SCRIPT_DIR}/../../.." && pwd )"
# Keep test configuration separate from the server build. Override explicitly
# to reuse another user-owned test build directory.
BUILD_DIR="${DC_TEST_BUILD_DIR:-${SCRIPT_DIR}/../build/tests}"
mkdir -p "${BUILD_DIR}"
BUILD_DIR="$( cd "${BUILD_DIR}" && pwd )"

if [[ ! -w "${BUILD_DIR}" ]]; then
  echo "Test build directory is not writable: ${BUILD_DIR}" >&2
  echo "Set DC_TEST_BUILD_DIR to a directory owned by your user; do not use sudo." >&2
  exit 1
fi

echo "=========================================================="
echo " Dungeon Clear Test Runner"
echo "=========================================================="
echo "Repository Root: ${REPO_ROOT}"
echo "Build Directory: ${BUILD_DIR}"
echo "----------------------------------------------------------"

# Configure with unit testing enabled
echo "Configuring build system with BUILD_TESTING=ON..."
cmake -S "${REPO_ROOT}" -B "${BUILD_DIR}" -DBUILD_TESTING=ON "$@"

# Compile the dungeon_clear_tests target specifically
echo "Building dungeon_clear_tests target..."
cmake --build "${BUILD_DIR}" --target dungeon_clear_tests -j"$(nproc)"

# Run the DungeonClear test cases. The binary is the module's standalone suite,
# so this covers the DungeonClear* fixtures plus the Dc* suites (the approach
# regressions and the replay harness round-trip/fixture runner).
#
# Per-dungeon suites are named after their dungeon and so match NONE of those
# patterns — they have to be listed by name or they are silently skipped. The
# route probes among them skip themselves unless DC_PROBE_MMAPS names a dir with
# mmaps/ for the map, so listing one costs nothing when the navmesh is absent.
echo "Running unit tests..."
cd "${BUILD_DIR}"
./dungeon_clear_tests --gtest_filter='*DungeonClear*:Dc*:RoomAggro*:BossRoster*:DungeonEvent*:EventBuilder*:CullingOfStratholme*:TrialOfTheChampion*:Oculus*:SealedEncounter*'

echo "----------------------------------------------------------"
echo "Tests completed successfully!"
echo "=========================================================="
