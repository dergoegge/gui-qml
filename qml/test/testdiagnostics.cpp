// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testdiagnostics.h>

#include <QJsonArray>
#include <QLoggingCategory>
#include <QMutex>
#include <QMutexLocker>

#include <deque>

namespace {
//! Messages kept before the oldest is evicted.
constexpr size_t MESSAGES_MAX{1024};
//! Longest message text retained; QML errors can carry long stack traces.
constexpr int MESSAGE_LENGTH_MAX{2000};

QMutex g_mutex;
std::deque<TestDiagnostics::Entry> g_entries;
quint64 g_next_seq{1};
quint64 g_dropped{0};

QtMessageHandler g_chained_handler{nullptr};

void RecordingMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    TestDiagnostics::Record(type, context, message);
    if (g_chained_handler) g_chained_handler(type, context, message);
}
} // namespace

void TestDiagnostics::Record(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    Entry entry;
    entry.type = type;
    entry.category = context.category ? QString::fromUtf8(context.category) : QString{};
    entry.file = context.file ? QString::fromUtf8(context.file) : QString{};
    entry.line = context.line;
    entry.text = message.size() > MESSAGE_LENGTH_MAX
                     ? message.left(MESSAGE_LENGTH_MAX) + QStringLiteral("...")
                     : message;

    QMutexLocker locker(&g_mutex);
    entry.seq = g_next_seq++;
    g_entries.push_back(std::move(entry));
    while (g_entries.size() > MESSAGES_MAX) {
        g_entries.pop_front();
        ++g_dropped;
    }
}

void TestDiagnostics::InstallRecorder()
{
    QtMessageHandler previous = qInstallMessageHandler(RecordingMessageHandler);
    // Guard against chaining to ourselves if called twice.
    if (previous != RecordingMessageHandler) g_chained_handler = previous;
}

std::vector<TestDiagnostics::Entry> TestDiagnostics::Since(quint64 since_seq)
{
    QMutexLocker locker(&g_mutex);
    std::vector<Entry> result;
    for (const Entry& entry : g_entries) {
        if (entry.seq > since_seq) result.push_back(entry);
    }
    return result;
}

quint64 TestDiagnostics::LatestSeq()
{
    QMutexLocker locker(&g_mutex);
    return g_next_seq - 1;
}

quint64 TestDiagnostics::DroppedCount()
{
    QMutexLocker locker(&g_mutex);
    return g_dropped;
}

QString TestDiagnostics::LevelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return QStringLiteral("unknown");
}

QJsonObject TestDiagnostics::ToJson(quint64 since_seq)
{
    QJsonArray entries;
    for (const Entry& entry : Since(since_seq)) {
        QJsonObject json_entry;
        json_entry[QStringLiteral("seq")] = static_cast<double>(entry.seq);
        json_entry[QStringLiteral("level")] = LevelName(entry.type);
        json_entry[QStringLiteral("text")] = entry.text;
        if (!entry.category.isEmpty() && entry.category != QStringLiteral("default")) {
            json_entry[QStringLiteral("category")] = entry.category;
        }
        if (!entry.file.isEmpty()) {
            json_entry[QStringLiteral("file")] = entry.file;
            json_entry[QStringLiteral("line")] = entry.line;
        }
        entries.append(json_entry);
    }

    QJsonObject result;
    result[QStringLiteral("seq")] = static_cast<double>(LatestSeq());
    result[QStringLiteral("dropped")] = static_cast<double>(DroppedCount());
    result[QStringLiteral("entries")] = entries;
    return result;
}
