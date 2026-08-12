// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTTREE_H
#define BITCOIN_QML_TEST_TESTTREE_H

#include <QByteArray>
#include <QJsonObject>
#include <QPointF>
#include <QSet>
#include <QString>

class QObject;
class QQmlApplicationEngine;

/// Serializes the live QML object tree for external test drivers.
///
/// Where the objectName-addressed commands on TestBridge answer questions
/// about a known control, this answers "what is on screen right now" in a
/// single round trip: enough structure, geometry and properties for a driver
/// to pick a target, click it by coordinate, and compare states across steps.
namespace TestTree {
struct Options {
    /// Stop serializing after this many nodes and report truncation.
    int max_nodes{4000};
    /// Depth limit, guarding against unexpected cycles in the object graph.
    int max_depth{64};
    /// Include declared properties on each node.
    bool include_props{true};
    /// Subtrees rooted at these objectNames are omitted entirely. Used to
    /// exclude continuously animating parts of the UI, which would otherwise
    /// keep the tree from ever looking settled.
    QSet<QString> ignore_object_names;
};

/// Serialize every root object of @p engine below a synthetic root node.
QJsonObject Serialize(QQmlApplicationEngine* engine, const Options& options, int* node_count, bool* truncated);

/// A compact canonical encoding of the tree, for detecting whether the UI
/// changed between two points in time. Cheaper than Serialize() because it
/// skips JSON construction.
QByteArray Signature(QQmlApplicationEngine* engine, const Options& options);

/// Deepest visible item containing @p scene_point, or nullptr.
QObject* HitTest(QQmlApplicationEngine* engine, const QPointF& scene_point);

/// QML type name of @p object, with the engine's `_QMLTYPE_<n>` suffix
/// removed. The suffix is assigned in registration order and is not stable
/// enough to appear in a test's expectations.
QString TypeName(const QObject* object);
} // namespace TestTree

#endif // BITCOIN_QML_TEST_TESTTREE_H
