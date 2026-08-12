// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTBRIDGE_H
#define BITCOIN_QML_TEST_TESTBRIDGE_H

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QLocalServer>
#include <QLocalSocket>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QSet>
#include <QString>

#include <vector>

class QQuickWindow;

/// Exposes QML object tree to external test scripts over a Unix domain socket.
/// Enabled only when compiled with ENABLE_TEST_AUTOMATION and launched with
/// --test-automation=<socket_path>.
///
/// Supported commands (JSON over newline-delimited stream):
///   {"cmd": "get_current_page"}
///   {"cmd": "get_context_property", "name": "<contextPropertyName>"}
///   {"cmd": "get_property", "objectName": "<name>", "prop": "<property>"}
///   {"cmd": "set_property", "objectName": "<name>", "prop": "<property>", "value": <json-value>}
///   {"cmd": "invoke", "objectName": "<name>", "method": "<method-or-signal-name>", "args": ["<arg>"]}
///   {"cmd": "invoke_property_object", "objectName": "<name>", "prop": "<QObject-property>", "method": "<method>", "args": ["<arg>"]}
///   {"cmd": "click", "objectName": "<name>"}
///   {"cmd": "set_text", "objectName": "<name>", "text": "<value>"}
///   {"cmd": "type_text", "objectName": "<name>", "text": "<value>"}
///   {"cmd": "wait_for_page", "page": "<objectName>", "timeout": <ms>}
///   {"cmd": "wait_for_property", "objectName": "<name>", "prop": "<property>", ...}
///   {"cmd": "get_text", "objectName": "<name>"}
///   {"cmd": "click_list_item", "objectName": "<view>", "index": <zero-based-row>, "childObjectName": "<optional-delegate-child>"}
///   {"cmd": "get_list_item_property", "objectName": "<view>", "index": <zero-based-row>, "prop": "<delegate-root-property>"}
///   {"cmd": "save_screenshot", "path": "<png_path>"}
///   {"cmd": "show_runtime_dialog", "message": "<text>", "style": <uint>, "question": <bool>}
///   {"cmd": "answer_runtime_dialog", "button": <uint>}
///   {"cmd": "list_objects"}
///   {"cmd": "close_window"}
///   {"cmd": "set_clipboard_text", "text": "<value>"}
///
/// Commands for coordinate-driven exploration, where the driver reads the
/// whole tree and acts on positions rather than on known objectNames:
///   {"cmd": "get_state", "sinceSeq": <diagnostic-seq>, "maxNodes": <int>, "props": <bool>, "ignore": ["<objectName>"]}
///   {"cmd": "click", "point": {"x": <int>, "y": <int>}, "button": "left"|"right"}
///   {"cmd": "press_key", "key": <Qt::Key>, "modifiers": <int>, "text": "<value>", "count": <int>}
///   {"cmd": "scroll", "point": {"x": <int>, "y": <int>}, "dx": <int>, "dy": <int>}
///   {"cmd": "drag", "from": {"x": <int>, "y": <int>}, "to": {"x": <int>, "y": <int>}, "steps": <int>, "delayMs": <int>}
///   {"cmd": "settle", "timeoutMs": <int>, "stableMs": <int>, "ignore": ["<objectName>"]}
class TestBridge : public QObject
{
    Q_OBJECT

public:
    /// Construct a TestBridge listening on @p socket_path.
    /// @p engine must remain valid for the lifetime of this object.
    explicit TestBridge(QQmlApplicationEngine* engine, const QString& socket_path, QObject* parent = nullptr);
    ~TestBridge() override;

private Q_SLOTS:
    void handleNewConnection();
    void handleClientData();
    void handleClientDisconnected();

private:
    struct NamedObjectEntry {
        QString object_name;
        QString class_name;
        int depth;
    };

    /// Find a QObject by objectName, searching the entire QML tree.
    QObject* findObjectByName(const QString& name) const;
    /// The application's main window, or nullptr before it exists.
    QQuickWindow* mainWindow() const;
    /// objectName (or class name) of the current page, empty when unknown.
    QString currentPageName() const;
    QObject* findNamedObjectInSubtree(QObject* root, const QString& name) const;
    QObject* findListItem(QObject* view_obj, int row) const;
    QObject* resolveCurrentLeafItem(QObject* item) const;

    /// Recursively collect all named objects from the QML tree.
    void collectNamedObjects(QObject* root, std::vector<NamedObjectEntry>& results, QSet<const QObject*>& visited, int depth) const;

    /// Process a single JSON command and return the JSON response.
    QByteArray processCommand(const QByteArray& json_cmd);
    void processClientCommands(QLocalSocket* client);

    /// Dispatch individual command handlers.
    QByteArray cmdGetCurrentPage();
    QByteArray cmdGetContextProperty(const QString& name);
    QByteArray cmdGetProperty(const QString& object_name, const QString& prop);
    QByteArray cmdSetProperty(const QString& object_name, const QString& prop, const QJsonValue& value);
    QByteArray cmdInvoke(const QString& object_name, const QString& method, const QJsonArray& args);
    QByteArray cmdInvokePropertyObject(const QString& object_name, const QString& prop, const QString& method, const QJsonArray& args);
    QByteArray cmdClick(const QString& object_name);
    QByteArray cmdSetText(const QString& object_name, const QString& text);
    QByteArray cmdTypeText(const QString& object_name, const QString& text);
    QByteArray cmdWaitForPage(const QString& page_name, int timeout_ms);
    QByteArray cmdWaitForProperty(const QString& object_name, const QString& prop, int timeout_ms, const QJsonValue& expected, bool has_expected, const QString& contains, bool non_empty);
    QByteArray cmdGetText(const QString& object_name);
    QByteArray cmdClickListItem(const QString& view_object_name, int row_index, const QString& delegate_child_object_name);
    QByteArray cmdGetListItemProperty(const QString& view_object_name, int row_index, const QString& prop);
    QByteArray cmdSaveScreenshot(const QString& path);
    QByteArray cmdShowRuntimeDialog(const QString& message, unsigned int style, bool question);
    QByteArray cmdAnswerRuntimeDialog(unsigned int button);
    QByteArray cmdListObjects();
    QByteArray cmdCloseWindow();
    QByteArray cmdSetClipboardText(const QString& text);
    QByteArray cmdGetState(const QJsonObject& request);
    QByteArray cmdClickPoint(const QJsonObject& request);
    QByteArray cmdPressKey(const QJsonObject& request);
    QByteArray cmdScroll(const QJsonObject& request);
    QByteArray cmdDrag(const QJsonObject& request);
    QByteArray cmdSettle(const QJsonObject& request);

    /// Build a JSON error response.
    static QByteArray errorResponse(const QString& message);

    QQmlApplicationEngine* m_engine;
    QLocalServer* m_server;
    std::vector<QLocalSocket*> m_clients;
    QHash<QLocalSocket*, QByteArray> m_read_buffers;
    bool m_processing_client_data{false};
    bool m_pending_client_data{false};
};

#endif // BITCOIN_QML_TEST_TESTBRIDGE_H
