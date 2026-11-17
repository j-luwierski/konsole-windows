/*
    SPDX-FileCopyrightText: 2026 Konsole contributors
    SPDX-License-Identifier: MIT
*/
#ifndef WINDOWSPROCESSUTILS_H
#define WINDOWSPROCESSUTILS_H

#include <QDir>
#include <QProcessEnvironment>
#include <QStringList>
#include <algorithm>
#include <vector>

namespace WindowsProcessUtils
{
// Quote each argument according to the Windows CRT command-line rules.
inline QString quoteArgument(const QString &argument)
{
    QString result(QLatin1Char('"'));
    int backslashes = 0;
    for (QChar ch : argument) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        result += QString(backslashes * (ch == QLatin1Char('"') ? 2 : 1), QLatin1Char('\\'));
        if (ch == QLatin1Char('"')) {
            result += QLatin1Char('\\');
        }
        result += ch;
        backslashes = 0;
    }
    result += QString(backslashes * 2, QLatin1Char('\\'));
    result += QLatin1Char('"');
    return result;
}

inline QString commandLine(const QString &program, const QStringList &arguments)
{
    QString result = quoteArgument(QDir::toNativeSeparators(program));
    for (const auto &argument : arguments) {
        result += QLatin1Char(' ');
        result += quoteArgument(argument);
    }
    return result;
}

inline std::vector<wchar_t> environmentBlock(const QStringList &overrides)
{
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto &entry : overrides) {
        const auto separator = entry.indexOf(QLatin1Char('='), 1);
        if (separator > 0) {
            environment.insert(entry.left(separator), entry.mid(separator + 1));
        }
    }
    auto entries = environment.toStringList();
    std::sort(entries.begin(), entries.end(), [](const QString &a, const QString &b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    std::vector<wchar_t> block;
    for (const auto &entry : entries) {
        const auto wide = entry.toStdWString();
        block.insert(block.end(), wide.begin(), wide.end());
        block.push_back(L'\0');
    }
    if (block.empty()) {
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}
}
#endif
