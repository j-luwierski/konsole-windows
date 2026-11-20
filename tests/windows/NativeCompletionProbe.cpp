/* SPDX-License-Identifier: MIT */
#include <cstdio>
#include <cwchar>
#include <cstdlib>
#include <string>
#include <windows.h>
#include "../../src/windows/NativeReport.h"
int wmain(int argc, wchar_t **argv)
{
    if (argc == 2 && std::wcscmp(argv[1], L"--launcher-crash") == 0) {
        SetErrorMode(GetErrorMode() | SEM_NOGPFAULTERRORBOX);
        Konsole::WindowsReport::initialize(L"crash-probe");
        RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
        return 99;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--launcher-check") == 0) {
        const wchar_t *root = _wgetenv(L"KONSOLE_LAUNCH_TEST_ROOT");
        if (!root) { return 31; }
        wchar_t cwd[32768]{}; GetCurrentDirectoryW(32768, cwd);
        if (std::wstring(cwd) != root) { return 32; }
        const auto same = [](const wchar_t *name, const std::wstring &expected) {
            const wchar_t *value = _wgetenv(name); return value && std::wstring(value) == expected;
        };
        if (!same(L"QT_PLUGIN_PATH", std::wstring(root) + L"\\bin")
            || !same(L"QT_QPA_PLATFORM_PLUGIN_PATH", std::wstring(root) + L"\\bin\\platforms")
            || !same(L"QT_QPA_PLATFORM", L"windows")
            || !same(L"MAGIC", std::wstring(root) + L"\\file\\magic.mgc")) { return 33; }
        return 0;
    }
    if (argc >= 2 && std::wcscmp(argv[1], L"--launcher-paths") == 0) {
        const wchar_t *root = _wgetenv(L"KONSOLE_LAUNCH_TEST_ROOT");
        if (!root) { return 34; }
        for (int i = 2; i + 1 < argc; i += 2) {
            const std::wstring expected = std::wstring(root)
                + (std::wcscmp(argv[i], L"--key-file") == 0 ? L"\\pairing key.txt" : L"");
            std::wstring supplied(argv[i + 1]);
            while (supplied.size() > 3 && (supplied.back() == L'\\' || supplied.back() == L'/')) { supplied.pop_back(); }
            if (supplied != expected) { return 35; }
        }
        return 0;
    }
    if (argc == 2 && std::wcscmp(argv[1], L"--help") == 0) {
        std::puts("Usage: new-command [options]\n  --alpha  First option\n  --beta   Second option\n  -S -s    Distinct case-sensitive switches");
        return 0;
    }
    std::puts("UNEXPECTED_EXECUTION"); return 3;
}
