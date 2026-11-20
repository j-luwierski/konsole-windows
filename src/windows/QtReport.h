/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "NativeReport.h"
#include <QDebug>
#include <QString>
#include <cstdio>
namespace Konsole::WindowsReport {
inline QtMessageHandler previousQtHandler = nullptr;
inline void qtHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // General Qt messages can contain arbitrary application data. Record
        // their origin, but only include text for platform-plugin loader errors.
        const QString category = QString::fromUtf8(context.category ? context.category : "default");
        const QString origin = QStringLiteral("severity=%1 category=%2 source=%3:%4")
            .arg(int(type)).arg(category, QString::fromUtf8(context.file ? context.file : "")).arg(context.line);
        event(L"Qt message", origin.toStdWString().c_str());
        if (category == QStringLiteral("qt.qpa.plugin")) {
            event(L"Qt platform error", message.left(4096).toStdWString().c_str());
        }
    }
    if (previousQtHandler) { previousQtHandler(type, context, message); }
    else {
        const QString formatted = qFormatLogMessage(type, context, message);
        OutputDebugStringW(formatted.toStdWString().c_str());
        const QByteArray console = formatted.toLocal8Bit();
        std::fwrite(console.constData(), 1, size_t(console.size()), stderr);
        std::fputc('\n', stderr);
    }
}
inline void initializeQt(const wchar_t *role)
{
    initialize(role);
    event(L"Qt runtime", QString::fromLatin1(qVersion()).toStdWString().c_str());
    previousQtHandler = qInstallMessageHandler(qtHandler);
}
}
