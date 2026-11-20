/* SPDX-License-Identifier: MIT */
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <windows.h>
int main(int argc, char **argv)
{
    if (argc == 2 && std::strcmp(argv[1], "--launcher-check") == 0) {
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
    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::puts("Usage: new-command [options]\n  --alpha  First option\n  --beta   Second option\n  -S -s    Distinct case-sensitive switches");
        return 0;
    }
    std::puts("UNEXPECTED_EXECUTION"); return 3;
}
