// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTDIAGNOSTICS_H
#define BITCOIN_QML_TEST_TESTDIAGNOSTICS_H

#include <QJsonObject>
#include <QString>
#include <QtGlobal>

#include <vector>

class QMessageLogContext;

/// Bounded, sequence-numbered record of Qt/QML diagnostic messages.
///
/// The QML engine reports binding errors, type errors and unhandled
/// exceptions through the Qt message handler. Property-based tests need to
/// attribute those messages to the action that triggered them, so entries
/// carry a monotonically increasing sequence number and callers ask for
/// everything newer than the last sequence they observed.
///
/// Record() is safe to call from any thread.
namespace TestDiagnostics {
struct Entry {
    quint64 seq{0};
    QtMsgType type{QtDebugMsg};
    QString category;
    QString text;
    QString file;
    int line{0};
};

/// Append a message to the ring buffer, dropping the oldest entry when full.
void Record(QtMsgType type, const QMessageLogContext& context, const QString& message);

/// Install a message handler that records every message and then forwards it
/// to the handler that was previously installed. Safe to call more than once.
void InstallRecorder();

/// Entries with a sequence number greater than @p since_seq, oldest first.
std::vector<Entry> Since(quint64 since_seq);

/// Sequence number of the most recently recorded message (0 when none).
quint64 LatestSeq();

/// How many entries have been evicted because the buffer was full.
quint64 DroppedCount();

/// Lowercase name of a message level, e.g. "warning".
QString LevelName(QtMsgType type);

/// Serialize the entries newer than @p since_seq, along with the sequence
/// number a caller should pass on its next request.
QJsonObject ToJson(quint64 since_seq);
} // namespace TestDiagnostics

#endif // BITCOIN_QML_TEST_TESTDIAGNOSTICS_H
