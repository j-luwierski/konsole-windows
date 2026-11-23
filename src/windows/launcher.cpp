/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "NativeReport.h"
#include <shellapi.h>
#include <string>
using namespace Konsole::WindowsReport;
#ifndef KONSOLE_PORTABLE_REVISION
#define KONSOLE_PORTABLE_REVISION "unknown"
#endif
static std::wstring quote(const std::wstring &value)
{
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t character : value) {
        if (character == L'\\') { ++slashes; continue; }
        if (character == L'\"') { result.append(slashes * 2 + 1, L'\\'); }
        else { result.append(slashes, L'\\'); }
        slashes = 0; result += character;
    }
    result.append(slashes * 2, L'\\'); result += L'\"'; return result;
}
static BOOL WINAPI launcherControl(DWORD type)
{
    // The foreground host handles Ctrl+C. Keep its supervising launcher alive
    // until the host has shut down and its exit status has been reported.
    return type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT;
}
static int launch(int argc, wchar_t **argv, bool remote)
{
    initialize(remote ? L"remote-launcher" : L"launcher");
    const std::string revision(KONSOLE_PORTABLE_REVISION);
    event(L"source revision", std::wstring(revision.begin(), revision.end()).c_str());
    bool checkOnly = false, quiet = false;
    std::wstring arguments;
    for (int i = 1; argv && i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--check-portable") { checkOnly = true; }
        else if (std::wstring(argv[i]) == L"--no-error-dialog") { quiet = true; }
        else {
            const std::wstring option(argv[i]);
            arguments += L" " + quote(option);
            if (remote && (option == L"--root" || option == L"--cwd" || option == L"--key-file"
                           || option == L"--generate-key" || option == L"--shell") && i + 1 < argc) {
                const std::wstring supplied(argv[++i]);
                if (option == L"--shell" && supplied.find_first_of(L"\\/:") == std::wstring::npos) {
                    arguments += L" " + quote(supplied); continue;
                }
                wchar_t absolute[32768]{};
                const DWORD size = GetFullPathNameW(supplied.c_str(), DWORD(std::size(absolute)), absolute, nullptr);
                if (!size || size >= std::size(absolute)) {
                    event(L"remote path error", L"Cannot resolve a supplied host path."); return 8;
                }
                arguments += L" " + quote(absolute);
                if ((option == L"--root" || option == L"--cwd") && !directory(absolute)) {
                    event(L"unavailable remote directory", absolute); return 8;
                }
            }
        }
    }
    const auto fail = [&](const std::wstring &message, int status) {
        event(L"launcher error", message.c_str());
        if (!quiet && !remote) {
            const auto text = message + L"\n\nReport / Raport:\n" + state().path;
            MessageBoxW(nullptr, text.c_str(), L"Konsole portable", MB_OK | MB_ICONERROR);
        }
        return status;
    };
    const auto location = executableDirectory();
    std::wstring root = location;
    if (!regularFile(root + L"\\bin\\konsole.exe")) {
        const auto slash = location.find_last_of(L"\\/");
        if (slash != std::wstring::npos && regularFile(location + L"\\konsole.exe")) { root = location.substr(0, slash); }
    }
    if (root.size() == 2 && root[1] == L':') { root += L'\\'; }
    const auto bin = root + L"\\bin";
    event(L"portable root", root.c_str());
    const wchar_t *required[] = {
        L"bin\\konsole.exe", L"bin\\konsoleapp.dll", L"bin\\konsoleprivate.dll",
        L"bin\\Qt6Core.dll", L"bin\\Qt6Gui.dll", L"bin\\Qt6Widgets.dll", L"bin\\Qt6Concurrent.dll",
        L"bin\\platforms\\qwindows.dll", L"bin\\konsole-file-host.exe", L"bin\\Start-RemoteHost.exe", L"bin\\Start-RemoteHost.ps1", L"bin\\Start-RemoteHost.cmd",
        L"bin\\KonsoleCompletion.ps1", L"powershell\\pwsh.exe", L"powershell\\coreclr.dll",
        L"powershell\\Modules\\PSReadLine\\PSReadLine.psd1",
        L"powershell\\Modules\\PSReadLine\\Microsoft.PowerShell.PSReadLine.dll",
        L"file\\file.exe", L"file\\magic.mgc"
    };
    bool complete = true;
    for (const auto relative : required) {
        const auto path = root + L"\\" + relative;
        if (!regularFile(path)) { complete = false; event(L"missing or unavailable portable file", path.c_str()); }
    }
    if (!complete) {
        return fail(L"Incomplete or unavailable portable package. Extract the ENTIRE ZIP into one folder.\n"
                    L"Niekompletna lub niedostepna paczka portable. Rozpakuj CALY ZIP do jednego folderu.", 2);
    }
    // Keep global Windows settings untouched. Only the launched process receives
    // these package-relative paths, so foreign Qt installations cannot take over.

    const auto path = bin + L";" + root + L"\\powershell;" + root + L"\\file;" + environment(L"PATH");
    if (!SetEnvironmentVariableW(L"QT_PLUGIN_PATH", bin.c_str())
        || !SetEnvironmentVariableW(L"QT_QPA_PLATFORM_PLUGIN_PATH", (bin + L"\\platforms").c_str())
        || !SetEnvironmentVariableW(L"QT_QPA_PLATFORM", L"windows")
        || !SetEnvironmentVariableW(L"PATH", path.c_str())
        || !SetEnvironmentVariableW(L"MAGIC", (root + L"\\file\\magic.mgc").c_str())) {
        return fail(systemError(GetLastError()), 3);
    }
    std::wstring workingDirectory = environment(L"USERPROFILE");
    if (!directory(workingDirectory)) { workingDirectory = root; }
    event(L"child working directory", workingDirectory.c_str());
    event(L"Qt plugins", bin.c_str());
    event(L"portable validation", L"passed");
    if (checkOnly) { return 0; }
    const auto program = bin + (remote ? L"\\konsole-file-host.exe" : L"\\konsole.exe");
    if (remote) { SetConsoleCtrlHandler(launcherControl, TRUE); }
    std::wstring command = quote(program) + arguments;
    if (command.size() >= 32767) { return fail(L"Windows command line is too long.", 4); }
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    BOOL inheritHandles = FALSE;
    if (remote) {
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        bool usable = true;
        for (HANDLE handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError}) {
            if (!handle || handle == INVALID_HANDLE_VALUE) { usable = false; break; }
        }
        if (usable) {
            for (HANDLE handle : {startup.hStdInput, startup.hStdOutput, startup.hStdError}) {
                if (!SetHandleInformation(handle, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
                    return fail(systemError(GetLastError()), 9);
                }
            }
            startup.dwFlags |= STARTF_USESTDHANDLES;
            inheritHandles = TRUE;
        }
    }
    const DWORD previousMode = GetThreadErrorMode();
    SetThreadErrorMode(previousMode | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
    const BOOL started = CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, inheritHandles,
                                        CREATE_UNICODE_ENVIRONMENT, nullptr, workingDirectory.c_str(), &startup, &process);
    const DWORD error = started ? ERROR_SUCCESS : GetLastError();
    SetThreadErrorMode(previousMode, nullptr);
    if (!started) { return fail(systemError(error), 5); }
    CloseHandle(process.hThread);
    event(L"child process ID", std::to_wstring(process.dwProcessId).c_str());
    const DWORD wait = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 0;
    const bool gotStatus = wait == WAIT_OBJECT_0 && GetExitCodeProcess(process.hProcess, &status);
    const DWORD waitError = gotStatus ? ERROR_SUCCESS : GetLastError();
    CloseHandle(process.hProcess);
    if (!gotStatus) { return fail(systemError(waitError), 6); }
    wchar_t statusText[80]{};
    StringCchPrintfW(statusText, std::size(statusText), L"%lu (0x%08lx)", status, status);
    event(L"child exit status", statusText);
    if (status != 0) { return fail(L"Konsole stopped with status " + std::to_wstring(status), 7); }
    return 0;
}

#ifdef KONSOLE_REMOTE_LAUNCHER
int wmain(int argc, wchar_t **argv)
{
    const int status = launch(argc, argv, true);
    if (status != 0) {
        const auto text = L"Konsole remote launcher status " + std::to_wstring(status) + L". Report / Raport: " + state().path + L"\r\n";
        HANDLE output = GetStdHandle(STD_ERROR_HANDLE);
        DWORD mode = 0, written = 0;
        if (GetConsoleMode(output, &mode)) { WriteConsoleW(output, text.c_str(), DWORD(text.size()), &written, nullptr); }
        else {
            const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
            std::string bytes(size_t(length > 0 ? length : 1), '\0');
            if (length > 1) {
                WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, bytes.data(), length, nullptr, nullptr);
                WriteFile(output, bytes.data(), DWORD(length - 1), &written, nullptr);
            }
        }
    }
    return status;
}
#else
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) { initialize(L"launcher"); event(L"launcher error", L"Cannot parse the Windows command line."); return 1; }
    const int status = launch(argc, argv, false);
    LocalFree(argv); return status;
}
#endif
