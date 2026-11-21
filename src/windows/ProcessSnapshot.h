/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QStringList>
#include <QByteArray>
#include <QHash>
#include <QSet>
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
namespace Konsole::WindowsProcess {
inline DWORD sshDescendant(DWORD root)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return root; }
    QHash<DWORD, DWORD> parents;
    QSet<DWORD> ssh;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            parents.insert(entry.th32ProcessID, entry.th32ParentProcessID);
            if (QString::fromWCharArray(entry.szExeFile).compare(QStringLiteral("ssh.exe"), Qt::CaseInsensitive) == 0) {
                ssh.insert(entry.th32ProcessID);
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    for (DWORD candidate : ssh) {
        QSet<DWORD> visited;
        DWORD parent = candidate;
        while (parent && !visited.contains(parent)) {
            if (parent == root) { return candidate; }
            visited.insert(parent); parent = parents.value(parent);
        }
    }
    return root;
}
inline QStringList arguments(DWORD pid)
{
    // ProcessCommandLineInformation returns a counted UNICODE_STRING followed
    // by the command line. Supported by the Windows versions required by ConPTY.
    using Query = LONG (WINAPI *)(HANDLE, ULONG, PVOID, ULONG, PULONG);
    const auto query = reinterpret_cast<Query>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationProcess"));
    if (!query) { return {}; }
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) { return {}; }
    ULONG size = 0; query(process, 60, nullptr, 0, &size);
    if (!size || size > 1024 * 1024) { CloseHandle(process); return {}; }
    QByteArray buffer(int(size), '\0');
    const LONG status = query(process, 60, buffer.data(), size, &size);
    CloseHandle(process);
    if (status < 0) { return {}; }
    struct UnicodeString { USHORT length; USHORT maximumLength; PWSTR data; };
    const auto *value = reinterpret_cast<const UnicodeString *>(buffer.constData());
    const auto start = reinterpret_cast<quintptr>(buffer.constData());
    const auto data = reinterpret_cast<quintptr>(value->data);
    if (data < start || data + value->length > start + quintptr(buffer.size())) { return {}; }
    const QString command = QString::fromWCharArray(value->data, value->length / sizeof(wchar_t));
    int count = 0;
    LPWSTR *parts = CommandLineToArgvW(reinterpret_cast<LPCWSTR>(command.utf16()), &count);
    QStringList result;
    if (parts) {
        for (int i = 0; i < count; ++i) { result.append(QString::fromWCharArray(parts[i])); }
        LocalFree(parts);
    }
    return result;
}
}
