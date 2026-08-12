# Antithesis harness for the QML GUI

Runs [Bombadil](https://github.com/antithesishq/bombadil) against
`bitcoin-core-app` inside Antithesis: the UI is explored autonomously and the
properties in [`spec/gui.ts`](spec/gui.ts) are checked on every state.

Bombadil drives the app through the test automation bridge documented in
[`doc/test-bridge.md`](../../doc/test-bridge.md), so the image must be built with
`-DENABLE_TEST_AUTOMATION=ON`.

## Layout

| Path | Purpose |
|---|---|
| `Dockerfile.app` | Builds the app and assembles the system-under-test image |
| `Dockerfile.bombadil` | Builds the Bombadil CLI with only the QML driver |
| `docker-compose.yaml` | The Antithesis environment: one container |
| `entrypoint.sh` | Reports setup completion, then idles |
| `test/v1/gui/singleton_driver_explore.sh` | The test command Antithesis runs |
| `spec/gui.ts` | Properties and action set for this GUI |

Both processes live in one container on purpose: the bridge is a Unix domain
socket, so the driver and the application have to share a filesystem.

## Building

Bombadil first, from a checkout of that repository:

```bash
docker build -f test/antithesis/Dockerfile.bombadil -t bombadil-qml /path/to/bombadil
```

Then the system under test, from this repository's root:

```bash
docker build \
  -f test/antithesis/Dockerfile.app \
  --build-context bombadil=docker-image://bombadil-qml \
  -t gui-qml-sut:latest .
```

Building the app image takes a while: it compiles Bitcoin Core and the GUI from
source.

## Running locally

The same image runs outside Antithesis, which is the quickest way to check the
harness before pushing images:

```bash
docker run --rm gui-qml-sut:latest \
  sh -c '/opt/antithesis/test/v1/gui/singleton_driver_explore.sh'
```

Set `BOMBADIL_TIME_LIMIT` to shorten or extend the run:

```bash
docker run --rm -e BOMBADIL_TIME_LIMIT=2m gui-qml-sut:latest \
  sh -c '/opt/antithesis/test/v1/gui/singleton_driver_explore.sh'
```

The run prints each state and the action taken, and reports property violations
as they happen. `trace.jsonl` under the output directory records every state,
and `bombadil qml test --reproduce <trace>` replays it.

## Launching in Antithesis

Push both images to the Antithesis registry, then launch with this directory's
`docker-compose.yaml` as the environment. Bombadil reports each property to the
Antithesis SDK on every state, so properties appear in the triage report
without further wiring.

## What the run covers

From the default QML specification:

- the QML engine never reports a binding, type or reference error
- nothing logs at critical or fatal level
- the application does not exit while being explored
- every state offers something to click, so exploration never dead-ends
- the main page stack always knows which page it is showing

Specific to this GUI, from `spec/gui.ts`:

- translated strings are always substituted (no `%1` reaching the screen)
- no amount on screen exceeds the total supply, or is negative
- no page renders blank
- an open popup or dialog always has something to click

## Notes

- `--ignore blockClock` excludes the block clock from the observed state. It
  animates continuously, so leaving it in means the UI never looks settled and
  every state differs from the last for reasons unrelated to the test.
- Controls whose name or label matches `quit`, `reset`, `delete` and similar are
  left alone by the default action set, so a run does not end by shutting the
  application down a few steps in. `allClicks` in the default actions module
  includes them if that is what you want to test.
- The node runs on regtest with `connect=0`, so a run never reaches a real peer.
