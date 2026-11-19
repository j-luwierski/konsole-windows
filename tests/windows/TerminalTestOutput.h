/* SPDX-License-Identifier: MIT */
#pragma once
#include <QByteArray>
#include <QRegularExpression>
#include <QString>
// OSC directory reports and SGR/cursor updates can occur between output bytes.
// Test rendered text rather than requiring text to be contiguous in the VT wire stream.
inline QByteArray terminalTestText(const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes);
    static const QRegularExpression osc(QStringLiteral("\x1b\\][^\\x07\\x1b]*(?:\\x07|\\x1b\\\\)"));
    static const QRegularExpression csi(QStringLiteral("\x1b\\[[0-?]*[ -/]*[@-~]"));
    text.remove(osc); text.remove(csi);
    return text.toUtf8();
}
