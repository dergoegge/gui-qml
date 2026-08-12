#!/bin/sh
# Explore the GUI against test/antithesis/spec/gui.ts.
#
# A singleton driver runs once and is expected to finish, so the run is bounded
# by --time-limit. Bombadil starts and stops the application itself, on a fresh
# regtest datadir, which keeps each run independent.
set -eu

TIME_LIMIT="${BOMBADIL_TIME_LIMIT:-10m}"
OUTPUT_DIR="${ANTITHESIS_OUTPUT_DIR:-/tmp}/bombadil"

# The specification is bundled relative to the working directory.
cd /opt/spec

exec bombadil qml test \
    --specification ./gui.ts \
    --time-limit "${TIME_LIMIT}" \
    --ignore blockClock \
    --settle-timeout-ms 4000 \
    --output-path "${OUTPUT_DIR}" \
    --output-path-overwrite \
    -- /opt/bitcoin-core-app
