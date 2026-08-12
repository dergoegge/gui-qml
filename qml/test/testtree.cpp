// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testtree.h>

#include <QColor>
#include <QJsonArray>
#include <QList>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QStringList>
#include <QUrl>
#include <QVariant>

namespace {
//! Longest string value reported for a single property.
constexpr int PROPERTY_LENGTH_MAX{200};
//! Longest list reported for a single property.
constexpr int PROPERTY_LIST_MAX{16};

/// Properties worth reporting even though they come from a Qt base class
/// rather than from the QML file being tested.
const QStringList& WellKnownProperties()
{
    static const QStringList properties{
        QStringLiteral("text"),
        QStringLiteral("displayText"),
        QStringLiteral("placeholderText"),
        QStringLiteral("title"),
        QStringLiteral("checked"),
        QStringLiteral("checkable"),
        QStringLiteral("currentIndex"),
        QStringLiteral("currentText"),
        QStringLiteral("count"),
        QStringLiteral("readOnly"),
        QStringLiteral("echoMode"),
        QStringLiteral("value"),
        QStringLiteral("from"),
        QStringLiteral("to"),
        QStringLiteral("busy"),
        QStringLiteral("depth"),
        QStringLiteral("state"),
    };
    return properties;
}

/// Properties included in a tree signature. Deliberately short: the more
/// values a signature covers, the more often an unrelated change makes the UI
/// look unsettled.
const QStringList& SignatureProperties()
{
    static const QStringList properties{
        QStringLiteral("text"),
        QStringLiteral("checked"),
        QStringLiteral("currentIndex"),
        QStringLiteral("count"),
        QStringLiteral("busy"),
        QStringLiteral("depth"),
    };
    return properties;
}

bool IsGeneratedQmlTypeName(const QString& class_name)
{
    return class_name.contains(QStringLiteral("_QMLTYPE_")) ||
           class_name.contains(QStringLiteral("_QML_"));
}

bool HasSignalNamed(const QMetaObject* meta, const char* name)
{
    for (int i = 0; i < meta->methodCount(); ++i) {
        const QMetaMethod method = meta->method(i);
        if (method.methodType() == QMetaMethod::Signal && method.name() == name) {
            return true;
        }
    }
    return false;
}

bool IsClickable(QObject* object)
{
    const QMetaObject* meta = object->metaObject();
    return HasSignalNamed(meta, "clicked") ||
           HasSignalNamed(meta, "tapped") ||
           meta->indexOfMethod("click()") >= 0 ||
           meta->indexOfMethod("trigger()") >= 0 ||
           meta->indexOfProperty("checkable") >= 0;
}

bool IsEditable(QObject* object)
{
    const QMetaObject* meta = object->metaObject();
    return meta->indexOfProperty("cursorPosition") >= 0 &&
           meta->indexOfProperty("text") >= 0;
}

QString TruncateString(const QString& value)
{
    return value.size() > PROPERTY_LENGTH_MAX
               ? value.left(PROPERTY_LENGTH_MAX) + QStringLiteral("...")
               : value;
}

/// Convert a property value to JSON, or return an undefined value for types a
/// test driver cannot act on (object pointers, JS values, opaque handles).
QJsonValue PropertyToJson(const QVariant& value)
{
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        return value.toBool();
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return static_cast<double>(value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return value.toDouble();
    case QMetaType::QString:
        return TruncateString(value.toString());
    case QMetaType::QUrl:
        return TruncateString(value.toUrl().toString());
    case QMetaType::QColor:
        return value.value<QColor>().name();
    case QMetaType::QStringList: {
        QJsonArray array;
        const QStringList list = value.toStringList();
        for (int i = 0; i < list.size() && i < PROPERTY_LIST_MAX; ++i) {
            array.append(TruncateString(list.at(i)));
        }
        return array;
    }
    default:
        // Enums arrive as their underlying integer type only when registered;
        // otherwise report the numeric value when one is available.
        if (value.metaType().flags().testFlag(QMetaType::IsEnumeration)) {
            return static_cast<double>(value.toLongLong());
        }
        return QJsonValue{QJsonValue::Undefined};
    }
}

/// Properties declared by the QML files under test, plus the well-known
/// subset from Qt base classes. Walking the metaobject chain and taking only
/// the properties each generated QML type adds keeps the payload to values the
/// UI actually declares, instead of every inherited QQuickItem property.
QJsonObject DeclaredProperties(QObject* object)
{
    QJsonObject result;

    for (const QMetaObject* meta = object->metaObject(); meta; meta = meta->superClass()) {
        if (!IsGeneratedQmlTypeName(QString::fromLatin1(meta->className()))) continue;
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            if (!property.isReadable()) continue;
            const QString name = QString::fromLatin1(property.name());
            if (result.contains(name)) continue;
            const QJsonValue json_value = PropertyToJson(property.read(object));
            if (!json_value.isUndefined()) result[name] = json_value;
        }
    }

    for (const QString& name : WellKnownProperties()) {
        if (result.contains(name)) continue;
        const QByteArray latin1_name = name.toLatin1();
        if (object->metaObject()->indexOfProperty(latin1_name.constData()) < 0) continue;
        const QJsonValue json_value = PropertyToJson(object->property(latin1_name.constData()));
        if (!json_value.isUndefined()) result[name] = json_value;
    }

    return result;
}

/// Children of @p object in paint order, skipping ones already visited.
///
/// Visual children come first because that is the order they are drawn in,
/// which makes the last match at a coordinate the topmost one. Plain QObject
/// children are included when named, so that models and helpers declared in
/// QML remain observable.
QList<QObject*> ChildrenOf(QObject* object, QSet<const QObject*>& visited)
{
    QList<QObject*> children;

    auto append = [&](QObject* child) {
        if (!child || visited.contains(child)) return;
        visited.insert(child);
        children.append(child);
    };

    if (auto* window = qobject_cast<QQuickWindow*>(object)) {
        if (QQuickItem* content = window->contentItem()) {
            visited.insert(content);
            for (QQuickItem* child : content->childItems()) append(child);
        }
    } else if (auto* item = qobject_cast<QQuickItem*>(object)) {
        for (QQuickItem* child : item->childItems()) append(child);
    }

    for (QObject* child : object->children()) {
        if (qobject_cast<QQuickItem*>(child) || !child->objectName().isEmpty()) {
            append(child);
        }
    }

    return children;
}

bool IsVisibleNode(QObject* object)
{
    if (auto* window = qobject_cast<QQuickWindow*>(object)) return window->isVisible();
    if (auto* item = qobject_cast<QQuickItem*>(object)) return item->isVisible();
    const QVariant visible = object->property("visible");
    return !visible.isValid() || visible.toBool();
}

/// Bounding box in scene coordinates, or an invalid rect for non-visual objects.
QRectF SceneRect(QObject* object)
{
    if (auto* window = qobject_cast<QQuickWindow*>(object)) {
        return QRectF(0, 0, window->width(), window->height());
    }
    if (auto* item = qobject_cast<QQuickItem*>(object)) {
        const QPointF origin = item->mapToScene(QPointF(0, 0));
        return QRectF(origin.x(), origin.y(), item->width(), item->height());
    }
    return QRectF{};
}

struct SerializeState {
    const TestTree::Options& options;
    QSet<const QObject*>& visited;
    int node_count{0};
    bool truncated{false};
};

QJsonObject SerializeNode(QObject* object, SerializeState& state, int depth)
{
    QJsonObject node;
    ++state.node_count;

    node[QStringLiteral("type")] = TestTree::TypeName(object);
    if (!object->objectName().isEmpty()) {
        node[QStringLiteral("objectName")] = object->objectName();
    }

    const QRectF rect = SceneRect(object);
    if (rect.isValid() || qobject_cast<QQuickItem*>(object)) {
        QJsonArray rect_array;
        rect_array.append(qRound(rect.x()));
        rect_array.append(qRound(rect.y()));
        rect_array.append(qRound(rect.width()));
        rect_array.append(qRound(rect.height()));
        node[QStringLiteral("rect")] = rect_array;
    }

    const bool visible = IsVisibleNode(object);
    node[QStringLiteral("visible")] = visible;
    const QVariant enabled = object->property("enabled");
    node[QStringLiteral("enabled")] = !enabled.isValid() || enabled.toBool();

    if (auto* item = qobject_cast<QQuickItem*>(object)) {
        if (item->hasActiveFocus()) node[QStringLiteral("focus")] = true;
    }
    if (IsClickable(object)) node[QStringLiteral("clickable")] = true;
    if (IsEditable(object)) node[QStringLiteral("editable")] = true;

    if (state.options.include_props) {
        const QJsonObject properties = DeclaredProperties(object);
        if (!properties.isEmpty()) node[QStringLiteral("props")] = properties;
    }

    // An invisible subtree cannot be interacted with and would only add
    // noise, so the node is reported without descending into it.
    if (!visible || depth >= state.options.max_depth) return node;

    QJsonArray children;
    for (QObject* child : ChildrenOf(object, state.visited)) {
        if (state.options.ignore_object_names.contains(child->objectName())) continue;
        if (state.node_count >= state.options.max_nodes) {
            state.truncated = true;
            break;
        }
        children.append(SerializeNode(child, state, depth + 1));
    }
    if (!children.isEmpty()) node[QStringLiteral("children")] = children;

    return node;
}

void SignatureNode(QObject* object, const TestTree::Options& options, QSet<const QObject*>& visited, int depth, QByteArray& out)
{
    out.append(TestTree::TypeName(object).toUtf8());
    if (!object->objectName().isEmpty()) {
        out.append('#');
        out.append(object->objectName().toUtf8());
    }

    const QRectF rect = SceneRect(object);
    out.append('@');
    out.append(QByteArray::number(qRound(rect.x())));
    out.append(',');
    out.append(QByteArray::number(qRound(rect.y())));
    out.append(',');
    out.append(QByteArray::number(qRound(rect.width())));
    out.append(',');
    out.append(QByteArray::number(qRound(rect.height())));

    const bool visible = IsVisibleNode(object);
    out.append(visible ? 'v' : '-');
    const QVariant enabled = object->property("enabled");
    out.append(!enabled.isValid() || enabled.toBool() ? 'e' : '-');

    for (const QString& name : SignatureProperties()) {
        const QByteArray latin1_name = name.toLatin1();
        if (object->metaObject()->indexOfProperty(latin1_name.constData()) < 0) continue;
        const QJsonValue value = PropertyToJson(object->property(latin1_name.constData()));
        if (value.isUndefined()) continue;
        out.append(' ');
        out.append(latin1_name);
        out.append('=');
        out.append(value.toVariant().toString().toUtf8());
    }

    if (!visible || depth >= options.max_depth) return;

    out.append('(');
    for (QObject* child : ChildrenOf(object, visited)) {
        if (options.ignore_object_names.contains(child->objectName())) continue;
        SignatureNode(child, options, visited, depth + 1, out);
        out.append(';');
    }
    out.append(')');
}

void HitTestNode(QObject* object, const QPointF& point, QSet<const QObject*>& visited, int depth, QObject** result)
{
    if (!IsVisibleNode(object) || depth > 64) return;

    if (qobject_cast<QQuickItem*>(object) && SceneRect(object).contains(point)) {
        *result = object;
    }

    for (QObject* child : ChildrenOf(object, visited)) {
        HitTestNode(child, point, visited, depth + 1, result);
    }
}
} // namespace

QString TestTree::TypeName(const QObject* object)
{
    QString class_name = QString::fromLatin1(object->metaObject()->className());
    for (const QString& marker : {QStringLiteral("_QMLTYPE_"), QStringLiteral("_QML_")}) {
        const int index = class_name.indexOf(marker);
        if (index > 0) class_name.truncate(index);
    }
    return class_name;
}

QJsonObject TestTree::Serialize(QQmlApplicationEngine* engine, const Options& options, int* node_count, bool* truncated)
{
    QSet<const QObject*> visited;
    SerializeState state{options, visited};

    QJsonArray roots;
    for (QObject* root : engine->rootObjects()) {
        if (!root || visited.contains(root)) continue;
        visited.insert(root);
        if (options.ignore_object_names.contains(root->objectName())) continue;
        roots.append(SerializeNode(root, state, 1));
    }

    QJsonObject tree;
    tree[QStringLiteral("type")] = QStringLiteral("Application");
    tree[QStringLiteral("visible")] = true;
    tree[QStringLiteral("enabled")] = true;
    tree[QStringLiteral("children")] = roots;

    if (node_count) *node_count = state.node_count;
    if (truncated) *truncated = state.truncated;
    return tree;
}

QByteArray TestTree::Signature(QQmlApplicationEngine* engine, const Options& options)
{
    QByteArray signature;
    QSet<const QObject*> visited;
    for (QObject* root : engine->rootObjects()) {
        if (!root || visited.contains(root)) continue;
        visited.insert(root);
        if (options.ignore_object_names.contains(root->objectName())) continue;
        SignatureNode(root, options, visited, 1, signature);
        signature.append('\n');
    }
    return signature;
}

QObject* TestTree::HitTest(QQmlApplicationEngine* engine, const QPointF& scene_point)
{
    QObject* result = nullptr;
    QSet<const QObject*> visited;
    for (QObject* root : engine->rootObjects()) {
        if (!root || visited.contains(root)) continue;
        visited.insert(root);
        HitTestNode(root, scene_point, visited, 0, &result);
    }
    return result;
}
