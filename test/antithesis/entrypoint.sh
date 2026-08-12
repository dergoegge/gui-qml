#!/bin/sh
# The application is launched by the test command rather than by the container,
# so this only reports readiness and then stays alive.
set -eu

# Mirrors the SDK's setup-complete lifecycle event. Antithesis starts injecting
# faults and running test commands once it arrives.
if [ -n "${ANTITHESIS_OUTPUT_DIR:-}" ]; then
    printf '%s\n' '{"antithesis_setup":{"status":"complete","details":null}}' \
        >> "${ANTITHESIS_OUTPUT_DIR}/sdk.jsonl"
fi

echo "gui-qml system under test ready"
exec sleep infinity
