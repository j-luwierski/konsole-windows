/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <strsafe.h>
#include <algorithm>
#include <string>
#include <iterator>
#include <vector>

namespace Konsole::WindowsReport {
// This layer deliberately has no Qt dependency: the launcher can report a
// missing Qt DLL before the terminal's entry point can run.
inline bool directory(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}
inline bool regularFile(const std::wstring &path)
{
    const DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
inline std::wstring environment(const wchar_t *name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size || size > 32768) { return {}; }
    std::wstring result(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(name, result.data(), size);
    if (!written || written >= size) { return {}; }
    result.resize(written); return result;
}
inline std::wstring executableDirectory()
{
    std::wstring path(32768, L'\0');
    const DWORD size = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
    if (!size || size == path.size()) { return {}; }
    path.resize(size); const auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash);
}
inline std::wstring systemError(DWORD code)
{
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&buffer), 0, nullptr);
    std::wstring result = L"Windows error " + std::to_wstring(code) + L": ";
    if (buffer) { result += buffer; LocalFree(buffer); }
    return result;
}
inline std::wstring reportDirectory()
{
    std::wstring base = environment(L"LOCALAPPDATA");
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (base.empty() || !directory(base)) {
            wchar_t temporary[32768]{};
            if (!GetTempPathW(DWORD(std::size(temporary)), temporary)) { return {}; }
            base = temporary;
        }
        const auto product = base + L"\\Konsole";
        const auto reports = product + L"\\reports";
        CreateDirectoryW(product.c_str(), nullptr);
        CreateDirectoryW(reports.c_str(), nullptr);
        if (directory(reports)) { return reports; }
        base.clear();
    }
    return {};
}
struct State {
    HANDLE handle = INVALID_HANDLE_VALUE;
    SRWLOCK lock = SRWLOCK_INIT;
    std::wstring path;
    LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
    explicit State(const wchar_t *role)
    {
        const auto pid = std::to_wstring(GetCurrentProcessId());
        if (environment(L"KONSOLE_REPORT_PID") == pid) {
            path = environment(L"KONSOLE_REPORT_FILE");
            if (!path.empty()) {
                handle = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
        }
        if (handle != INVALID_HANDLE_VALUE) { return; }
        const auto folder = reportDirectory();
        if (folder.empty()) { return; }
        // Keep the twenty newest closed reports. Open reports cannot be deleted.
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW((folder + L"\\*.log").c_str(), &entry);
        std::vector<WIN32_FIND_DATAW> files;
        if (search != INVALID_HANDLE_VALUE) {
            do { if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { files.push_back(entry); } }
            while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        std::sort(files.begin(), files.end(), [](const auto &a, const auto &b) {
            return CompareFileTime(&a.ftLastWriteTime, &b.ftLastWriteTime) > 0;
        });
        for (size_t i = 19; i < files.size(); ++i) { DeleteFileW((folder + L"\\" + files[i].cFileName).c_str()); }
        SYSTEMTIME time{}; GetSystemTime(&time);
        wchar_t filename[160]{};
        StringCchPrintfW(filename, std::size(filename), L"\\%04u%02u%02u-%02u%02u%02u-%s-%lu-%llu.log",
                         time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
                         role, GetCurrentProcessId(), GetTickCount64());
        path = folder + filename;
        handle = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            SetEnvironmentVariableW(L"KONSOLE_REPORT_PID", pid.c_str());
            SetEnvironmentVariableW(L"KONSOLE_REPORT_FILE", path.c_str());
        }
    }
};
inline State &state(const wchar_t *role = L"konsole") { static State instance(role); return instance; }
inline void event(const wchar_t *name, const wchar_t *detail, bool nonBlocking = false)
{
    auto &report = state();
    if (report.handle == INVALID_HANDLE_VALUE) { return; }
    if (nonBlocking) { if (!TryAcquireSRWLockExclusive(&report.lock)) { return; } }
    else { AcquireSRWLockExclusive(&report.lock); }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(report.handle, &size) && size.QuadPart < 4 * 1024 * 1024) {
        SYSTEMTIME time{}; GetSystemTime(&time);
        wchar_t line[4608]{};
        StringCchPrintfW(line, std::size(line), L"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [%s] %.4096s\r\n",
                         time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds,
                         name, detail ? detail : L"");
        char utf8[18432]{};
        const int length = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, int(std::size(utf8)), nullptr, nullptr);
        DWORD written = 0;
        if (length > 1) { WriteFile(report.handle, utf8, DWORD(length - 1), &written, nullptr); }
        FlushFileBuffers(report.handle);
    }
    ReleaseSRWLockExclusive(&report.lock);
}
inline LONG WINAPI exceptionFilter(EXCEPTION_POINTERS *exception)
{
    // Best effort only; the independent launcher also records abnormal exit codes.
    wchar_t detail[1024]{};
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(exception->ExceptionRecord->ExceptionAddress), &module);
    wchar_t modulePath[768]{};
    if (module) { GetModuleFileNameW(module, modulePath, DWORD(std::size(modulePath))); }
    StringCchPrintfW(detail, std::size(detail), L"exception=0x%08lx address=%p module=%s offset=0x%llx",
                     exception->ExceptionRecord->ExceptionCode, exception->ExceptionRecord->ExceptionAddress, modulePath,
                     static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(exception->ExceptionRecord->ExceptionAddress)
                                                    - reinterpret_cast<ULONG_PTR>(module)));
    event(L"unhandled exception", detail, true);
    const auto previous = state().previousFilter;
    return previous && previous != exceptionFilter ? previous(exception) : EXCEPTION_CONTINUE_SEARCH;
}
inline void installExceptionHandler()
{
    const auto previous = SetUnhandledExceptionFilter(exceptionFilter);
    if (previous != exceptionFilter) { state().previousFilter = previous; }
}
inline void initialize(const wchar_t *role)
{
    state(role); installExceptionHandler();
    event(L"process", role);
    event(L"executable directory", executableDirectory().c_str());
    wchar_t cwd[32768]{}; GetCurrentDirectoryW(DWORD(std::size(cwd)), cwd);
    event(L"working directory", cwd);
    using VersionFunction = LONG(WINAPI *)(OSVERSIONINFOW *);
    const auto versionFunction = reinterpret_cast<VersionFunction>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version{}; version.dwOSVersionInfoSize = sizeof(version);
    if (versionFunction && versionFunction(&version) == 0) {
        const auto text = std::to_wstring(version.dwMajorVersion) + L"." + std::to_wstring(version.dwMinorVersion)
            + L" build " + std::to_wstring(version.dwBuildNumber);
        event(L"Windows", text.c_str());
    }
    SYSTEM_INFO system{}; GetNativeSystemInfo(&system);
    event(L"native architecture", std::to_wstring(system.wProcessorArchitecture).c_str());
    event(L"privacy", L"No terminal stream, command arguments, environment dump or pairing key is recorded.");
}
}
