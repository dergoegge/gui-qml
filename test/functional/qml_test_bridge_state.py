#!/usr/bin/env python3
# Copyright (c) 2026 The Bitcoin Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Tests for the coordinate-driven test bridge commands.

Covers get_state, click by point, press_key, scroll, drag and settle: the
commands a property-based driver uses to explore the UI without knowing an
objectName for everything it can see.

This test requires the binary to be built with -DENABLE_TEST_AUTOMATION=ON.
"""

import sys

from qml_driver import (
    QmlDriverError,
    actionable_nodes,
    find_node,
    node_center,
    walk_tree,
)
from qml_test_harness import QmlTestHarness, dump_qml_tree, parse_args

# Qt::Key values used below; the bridge takes raw Qt key codes.
KEY_ESCAPE = 0x01000000
KEY_TAB = 0x01000001

# Markers that only appear in genuine QML engine errors, as opposed to the
# assorted platform warnings a headless run produces.
QML_ERROR_MARKERS = ("TypeError:", "ReferenceError:", "SyntaxError:")

POST_ONBOARDING_TIMEOUT_MS = 30000


def assert_tree_invariants(tree):
    """Every node reports a type and visibility, and rects are 4 numbers."""
    for node in walk_tree(tree):
        assert "type" in node, f"Node without a type: {node}"
        assert isinstance(node["visible"], bool), f"Bad visible: {node}"
        assert isinstance(node["enabled"], bool), f"Bad enabled: {node}"
        rect = node.get("rect")
        if rect is not None:
            assert len(rect) == 4, f"Bad rect on {node['type']}: {rect}"
            assert all(isinstance(value, (int, float)) for value in rect), \
                f"Non-numeric rect on {node['type']}: {rect}"
        # Invisible subtrees are reported without their children.
        if not node["visible"]:
            assert "children" not in node, \
                f"Invisible node {node['type']} should not report children"


def qml_errors(diagnostics):
    """Diagnostic entries that look like QML engine errors."""
    return [
        entry for entry in diagnostics["entries"]
        if any(marker in entry["text"] for marker in QML_ERROR_MARKERS)
    ]


def run_tests():
    args = parse_args()
    harness = QmlTestHarness(socket_path=args.socket_path)
    gui = None
    try:
        harness.start()
        gui = harness.driver
        gui.wait_for_object("nodeSettingsButton", timeout_ms=POST_ONBOARDING_TIMEOUT_MS)

        # ── Test 1: get_state returns a usable snapshot ──────────
        print("\nTest 1: get_state")
        state = gui.get_state()
        assert state["tree"]["type"] == "Application", \
            f"Unexpected root node: {state['tree']}"
        assert state["nodeCount"] > 0, "Expected a non-empty tree"
        assert state["window"]["width"] > 0 and state["window"]["height"] > 0, \
            f"Expected a sized window, got {state['window']}"
        assert find_node(state["tree"], "mainPageStack") is not None, \
            "Expected mainPageStack in the tree"
        print(f"  {state['nodeCount']} nodes, window {state['window']['width']}"
              f"x{state['window']['height']}, page {state.get('currentPage')}")
        print("  PASSED")

        # ── Test 2: tree invariants ──────────────────────────────
        print("\nTest 2: tree invariants")
        assert_tree_invariants(state["tree"])
        page_stack = find_node(state["tree"], "mainPageStack")
        x, y, width, height = page_stack["rect"]
        assert width > 0 and height > 0, f"Empty mainPageStack rect: {page_stack['rect']}"
        assert x >= 0 and y >= 0, f"mainPageStack outside window: {page_stack['rect']}"
        print(f"  mainPageStack at {page_stack['rect']}")
        print("  PASSED")

        # ── Test 3: type names are stripped of engine suffixes ───
        print("\nTest 3: QML type names")
        suffixed = [
            node["type"] for node in walk_tree(state["tree"])
            if "_QMLTYPE_" in node["type"] or "_QML_" in node["type"]
        ]
        assert not suffixed, f"Unstripped generated type names: {suffixed[:5]}"
        print("  No generated type suffixes reported")
        print("  PASSED")

        # ── Test 4: diagnostics sequencing ──────────────────────
        print("\nTest 4: diagnostics")
        diagnostics = state["diagnostics"]
        assert "seq" in diagnostics and "entries" in diagnostics, \
            f"Unexpected diagnostics shape: {diagnostics}"
        for entry in diagnostics["entries"]:
            assert entry["seq"] > 0 and entry["level"], f"Bad entry: {entry}"
        # Asking for everything after the latest sequence yields nothing new
        # (barring messages emitted between the two calls, which must be newer).
        latest = diagnostics["seq"]
        follow_up = gui.get_state(since_seq=latest)["diagnostics"]
        assert all(entry["seq"] > latest for entry in follow_up["entries"]), \
            "sinceSeq did not filter already-observed messages"
        errors = qml_errors(diagnostics)
        assert not errors, f"QML errors during startup: {errors}"
        print(f"  {len(diagnostics['entries'])} message(s) up to seq {latest}, "
              f"no QML errors")
        print("  PASSED")

        # ── Test 5: actionable nodes are discoverable ───────────
        print("\nTest 5: actionable nodes")
        actionable = actionable_nodes(state)
        assert actionable, "Expected at least one clickable node on the shell"
        by_name = {node.get("objectName") for node in actionable}
        assert "nodeSettingsButton" in by_name, \
            f"nodeSettingsButton not reported as actionable; found {sorted(n for n in by_name if n)[:10]}"
        print(f"  {len(actionable)} actionable node(s)")
        print("  PASSED")

        # ── Test 6: click by point navigates ────────────────────
        print("\nTest 6: click by point")
        target = find_node(state["tree"], "nodeSettingsButton")
        point = node_center(target)
        hit = gui.click_point(*point)
        assert hit is not None, f"No item reported at {point}"
        print(f"  Clicked {point}, hit {hit}")
        settled = gui.settle_tree(timeout_ms=5000)
        assert settled["stable"], f"UI never settled after click: {settled}"
        print(f"  Settled after {settled['elapsedMs']}ms "
              f"({settled['revisions']} revision(s))")
        gui.wait_for_property("settings_display", "visible", True, timeout_ms=5000)
        print("  Reached node settings")
        print("  PASSED")

        # ── Test 7: clicks outside the window are rejected ──────
        print("\nTest 7: click outside the window")
        window = gui.get_state()["window"]
        try:
            gui.click_point(window["width"] + 50, 10)
            assert False, "Expected an error for a point outside the window"
        except QmlDriverError as e:
            assert "outside" in str(e), f"Unexpected error message: {e}"
            print(f"  Correctly rejected: {e}")
            print("  PASSED")

        # ── Test 8: keys, wheel and drag are delivered ─────────
        print("\nTest 8: press_key, scroll, drag")
        gui.press_key(KEY_TAB)
        gui.press_key(KEY_ESCAPE)
        centre = (window["width"] // 2, window["height"] // 2)
        gui.scroll(*centre, dy=-120)
        gui.drag(centre, (centre[0], centre[1] - 100), steps=5)
        settled = gui.settle_tree(timeout_ms=5000)
        assert settled["stable"], f"UI never settled after input: {settled}"
        after = gui.get_state(since_seq=latest)
        errors = qml_errors(after["diagnostics"])
        assert not errors, f"QML errors provoked by input: {errors}"
        print("  Input delivered without QML errors")
        print("  PASSED")

        # ── Test 9: ignoring subtrees ──────────────────────────
        print("\nTest 9: ignore filters subtrees")
        filtered = gui.get_state(ignore=["mainPageStack"])
        assert find_node(filtered["tree"], "mainPageStack") is None, \
            "Ignored subtree still present"
        assert filtered["nodeCount"] < state["nodeCount"], \
            "Ignoring a subtree did not reduce the node count"
        print(f"  {state['nodeCount']} nodes -> {filtered['nodeCount']} when ignoring "
              "mainPageStack")
        print("  PASSED")

        print("\n" + "=" * 50)
        print("All tests PASSED")
        print("=" * 50)

    except Exception as e:
        print(f"\nFAILED: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        if gui is not None:
            dump_qml_tree(gui)
        sys.exit(1)
    finally:
        harness.stop()


if __name__ == '__main__':
    run_tests()
