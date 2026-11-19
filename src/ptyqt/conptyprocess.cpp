/*
    SPDX-FileCopyrightText: 2019 Vitaly Petrov <v31337@gmail.com>
    SPDX-License-Identifier: MIT
*/
#include "conptyprocess.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QDir>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include "windowsprocessutils.h"
#include "windowsshellintegration.h"

#define READ_INTERVAL_MSEC 500

static QString windowsFailure(const QString &operation, DWORD error)
{
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    const QString detail = message ? QString::fromWCharArray(message).trimmed() : QString();
    if (message) { LocalFree(message); }
    return QStringLiteral("%1 (Windows error %2, 0x%3): %4")
        .arg(operation).arg(error).arg(error, 8, 16, QLatin1Char('0')).arg(detail);
}

HRESULT ConPtyProcess::createPseudoConsoleAndPipes(HPCON *phPC, HANDLE *phPipeIn, HANDLE *phPipeOut, qint16 cols, qint16 rows)
{
    HRESULT hr{E_UNEXPECTED};
    HANDLE hPipePTYIn{INVALID_HANDLE_VALUE};
    HANDLE hPipePTYOut{INVALID_HANDLE_VALUE};

    if (!CreatePipe(&hPipePTYIn, phPipeOut, nullptr, 0)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }
    if (!CreatePipe(phPipeIn, &hPipePTYOut, nullptr, 0)) {
        hr = HRESULT_FROM_WIN32(GetLastError());
    } else {
        hr = m_winContext.createPseudoConsole({cols, rows}, hPipePTYIn, hPipePTYOut, 0, phPC);
        CloseHandle(hPipePTYOut);
    }
    CloseHandle(hPipePTYIn);

    return hr;
}

// Initializes the specified startup info struct with the required properties
// and updates its thread attribute list with the specified ConPTY handle
HRESULT ConPtyProcess::initializeStartupInfoAttachedToPseudoConsole(STARTUPINFOEX *pStartupInfo, HPCON hPC)
{
    HRESULT hr{E_UNEXPECTED};

    if (pStartupInfo) {
        SIZE_T attrListSize{};

        // Explicit null standard handles let the child initialize its ConPTY
        // console handles instead of inheriting redirected handles from a CLI
        // launcher (including CTest or PowerShell).
        pStartupInfo->StartupInfo.dwFlags |= STARTF_USESTDHANDLES;
        pStartupInfo->StartupInfo.hStdInput = nullptr;
        pStartupInfo->StartupInfo.hStdOutput = nullptr;
        pStartupInfo->StartupInfo.hStdError = nullptr;
        pStartupInfo->StartupInfo.cb = sizeof(STARTUPINFOEX);

        // Get the size of the thread attribute list.
        InitializeProcThreadAttributeList(NULL, 1, 0, &attrListSize);

        // Allocate a thread attribute list of the correct size
        pStartupInfo->lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, attrListSize));

        // Initialize thread attribute list
        if (pStartupInfo->lpAttributeList && InitializeProcThreadAttributeList(pStartupInfo->lpAttributeList, 1, 0, &attrListSize)) {
            // Set Pseudo Console attribute
            hr = UpdateProcThreadAttribute(pStartupInfo->lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE, hPC, sizeof(HPCON), NULL, NULL)
                ? S_OK
                : HRESULT_FROM_WIN32(GetLastError());
        } else {
            hr = HRESULT_FROM_WIN32(GetLastError());
            if (pStartupInfo->lpAttributeList) {
                HeapFree(GetProcessHeap(), 0, pStartupInfo->lpAttributeList);
                pStartupInfo->lpAttributeList = nullptr;
            }
        }
    }
    return hr;
}

ConPtyProcess::ConPtyProcess()
    : IPtyProcess()
    , m_ptyHandler{INVALID_HANDLE_VALUE}
    , m_hPipeIn{INVALID_HANDLE_VALUE}
    , m_hPipeOut{INVALID_HANDLE_VALUE}
    , m_readThread(nullptr)
{
}

ConPtyProcess::~ConPtyProcess()
{
    m_aboutToDestruct = true;
    kill();
}

bool ConPtyProcess::startProcess(const QString &shellPath,
                                 const QStringList &arguments,
                                 const QString &workingDir,
                                 QStringList environment,
                                 qint16 cols,
                                 qint16 rows)
{
    if (!isAvailable()) {
        return false;
    }

    // already running
    if (m_ptyHandler != INVALID_HANDLE_VALUE)
        return false;

    m_lastError.clear();
    m_shellPath = QStandardPaths::findExecutable(shellPath);
    if (m_shellPath.isEmpty()) {
        m_lastError = QStringLiteral("ConPTY: executable not found: %1").arg(shellPath);
        return false;
    }
    if (cols <= 0 || rows <= 0) {
        m_lastError = QStringLiteral("ConPTY: terminal dimensions must be positive");
        return false;
    }
    if (!workingDir.isEmpty() && !QDir(workingDir).exists()) {
        m_lastError = QStringLiteral("ConPTY: working directory does not exist or is unavailable: %1").arg(workingDir);
        return false;
    }
    m_size = {cols, rows};
    m_aboutToDestruct = false;
    m_exitCode = -1;
    m_exitNotified = false;
    auto env = WindowsProcessUtils::environmentBlock(environment);
    const QString shellName = QFileInfo(m_shellPath).completeBaseName();
    const bool powerShell = shellName.compare(QStringLiteral("pwsh"), Qt::CaseInsensitive) == 0
        || shellName.compare(QStringLiteral("powershell"), Qt::CaseInsensitive) == 0;
    const QStringList launchArguments = powerShell ? WindowsShellIntegration::compactStoredArguments(arguments) : arguments;
    auto cmdArg = WindowsProcessUtils::commandLine(m_shellPath, launchArguments).toStdWString();
    if (cmdArg.size() >= 32767) {
        m_lastError = QStringLiteral("ConPTY: Windows command line is too long (%1 characters; maximum is 32766)").arg(cmdArg.size());
        return false;
    }
    const auto nativeProgram = QDir::toNativeSeparators(m_shellPath).toStdWString();
    const auto nativeDirectory = QDir::toNativeSeparators(workingDir).toStdWString();

    HRESULT hr{E_UNEXPECTED};

    //  Create the Pseudo Console and pipes to it
    hr = createPseudoConsoleAndPipes(&m_ptyHandler, &m_hPipeIn, &m_hPipeOut, cols, rows);

    if (S_OK != hr) {
        m_lastError = windowsFailure(QStringLiteral("ConPTY: creating console failed"), DWORD(hr));
        kill();
        return false;
    }

    // Initialize the necessary startup info struct
    hr = initializeStartupInfoAttachedToPseudoConsole(&m_shellStartupInfo, m_ptyHandler);
    if (S_OK != hr) {
        m_lastError = windowsFailure(QStringLiteral("ConPTY: initializing process attributes failed"), DWORD(hr));
        kill();
        return false;
    }

    // Return loader/drive errors to the terminal instead of blocking on a
    // system error dialog, including when running without a visible desktop.
    const DWORD previousErrorMode = GetThreadErrorMode();
    SetThreadErrorMode(previousErrorMode | SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, nullptr);
    hr = CreateProcessW(nativeProgram.c_str(), // Explicit executable avoids ambiguous paths
                       cmdArg.data(), // Command Line
                       NULL, // Process handle not inheritable
                       NULL, // Thread handle not inheritable
                       FALSE, // Inherit handles
                       EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, // Creation flags
                       env.data(), // Inherited environment plus profile overrides
                       workingDir.isEmpty() ? nullptr : nativeDirectory.c_str(),
                       &m_shellStartupInfo.StartupInfo, // Pointer to STARTUPINFO
                       &m_shellProcessInformation) // Pointer to PROCESS_INFORMATION
        ? S_OK
        : GetLastError();
    SetThreadErrorMode(previousErrorMode, nullptr);

    if (hr != S_OK) {
        m_lastError = windowsFailure(QStringLiteral("ConPTY: cannot create process"), DWORD(hr));
        kill();
        return false;
    }
    m_pid = m_shellProcessInformation.dwProcessId;

    // this code runned in separate thread
    m_readThread = QThread::create([this]() {
        m_readerThreadId = GetCurrentThreadId();
        // buffers
        const DWORD BUFF_SIZE{1024};
        char szBuffer[BUFF_SIZE]{};

        while (true) {
            DWORD dwBytesRead{};

            if (QThread::currentThread()->isInterruptionRequested()) {
                break;
            }
            // Read from the pipe
            BOOL result = ReadFile(m_hPipeIn, szBuffer, BUFF_SIZE, &dwBytesRead, NULL);

            if (!result || dwBytesRead == 0) {
                break;
            }
            {
                QMutexLocker locker(&m_bufferMutex);
                m_buffer.m_readBuffer.append(szBuffer, dwBytesRead);
            }
            m_buffer.emitReadyRead();
        }
    });

    connect(m_readThread, &QThread::finished, this, [this]() {
        if (m_aboutToDestruct) {
            return;
        }
        finishIfExited();
    });
    connect(&m_exitPoll, &QTimer::timeout, this, [this]() {
        // Wait for the process object, not an early exit-status change. Closing
        // ConPTY during process teardown can replace its requested status with
        // STATUS_CONTROL_C_EXIT. A signaled process may also legitimately exit 259.
        DWORD code = 0;
        if (WaitForSingleObject(m_shellProcessInformation.hProcess, 0) == WAIT_OBJECT_0
            && GetExitCodeProcess(m_shellProcessInformation.hProcess, &code)) {
            m_exitPoll.stop();
            m_exitCode = int(code);
            // Closing ConPTY releases the output pipe; the reader delivers the
            // remaining bytes and emits exited only after it has drained them.
            if (m_ptyHandler != INVALID_HANDLE_VALUE && m_ptyHandler != nullptr) {
                m_winContext.closePseudoConsole(m_ptyHandler);
                m_ptyHandler = INVALID_HANDLE_VALUE;
            }
            finishIfExited();
        }
    });
    m_exitPoll.setInterval(20);
    // start read thread
    m_readThread->start();
    m_exitPoll.start();

    return true;
}

void ConPtyProcess::finishIfExited()
{
    if (m_aboutToDestruct || m_exitNotified || !m_readThread || m_readThread->isRunning()) {
        return;
    }
    DWORD code = 0;
    if (WaitForSingleObject(m_shellProcessInformation.hProcess, 0) != WAIT_OBJECT_0
        || !GetExitCodeProcess(m_shellProcessInformation.hProcess, &code)) {
        return;
    }
    m_exitNotified = true;
    m_exitPoll.stop();
    m_exitCode = int(code);
    m_pid = 0;
    // Session must receive all trailing output before handling finished().
    Q_EMIT m_buffer.readyRead();
    Q_EMIT exited();
}

bool ConPtyProcess::resize(qint16 cols, qint16 rows)
{
    if (m_ptyHandler == INVALID_HANDLE_VALUE || m_ptyHandler == nullptr || cols <= 0 || rows <= 0) {
        return false;
    }

    bool res = SUCCEEDED(m_winContext.resizePseudoConsole(m_ptyHandler, {cols, rows}));

    if (res) {
        m_size = std::pair<qint16, qint16>(cols, rows);
    }

    return res;
}

bool ConPtyProcess::kill()
{
    const bool notifyExit = !m_aboutToDestruct && !m_exitNotified && m_pid > 0;
    m_aboutToDestruct = true;
    m_exitPoll.stop();
    disconnect(&m_exitPoll, nullptr, this, nullptr);
    const bool hadConsole = m_ptyHandler != INVALID_HANDLE_VALUE && m_ptyHandler != nullptr;
    if (hadConsole) {
        // Process creation can fail before the output reader is started.
        // Older Windows versions wait indefinitely in ClosePseudoConsole if
        // its output pipe is neither drained nor closed.
        if (!m_readThread && m_hPipeIn && m_hPipeIn != INVALID_HANDLE_VALUE) {
            CloseHandle(m_hPipeIn);
            m_hPipeIn = INVALID_HANDLE_VALUE;
        }
        // Keep the reader alive while ClosePseudoConsole drains its output.
        m_winContext.closePseudoConsole(m_ptyHandler);
        m_ptyHandler = INVALID_HANDLE_VALUE;
    }
    if (m_readThread) {
        disconnect(m_readThread, nullptr, this, nullptr);
        m_readThread->requestInterruption();
        // ReadFile is synchronous; cancellation wakes it without terminating a Qt thread.
        HANDLE thread = OpenThread(THREAD_TERMINATE, FALSE, m_readerThreadId);
        if (thread) {
            CancelSynchronousIo(thread);
            CloseHandle(thread);
        }
        m_readThread->wait();
        delete m_readThread;
        m_readThread = nullptr;
    }
    if (notifyExit) {
        DWORD code = 0;
        if (WaitForSingleObject(m_shellProcessInformation.hProcess, 0) == WAIT_OBJECT_0
            && GetExitCodeProcess(m_shellProcessInformation.hProcess, &code)) {
            m_exitCode = int(code);
        }
    }
    for (HANDLE *handle : {&m_hPipeIn, &m_hPipeOut, &m_shellProcessInformation.hThread, &m_shellProcessInformation.hProcess}) {
        if (*handle && *handle != INVALID_HANDLE_VALUE) {
            CloseHandle(*handle);
        }
        *handle = INVALID_HANDLE_VALUE;
    }
    if (m_shellStartupInfo.lpAttributeList) {
        DeleteProcThreadAttributeList(m_shellStartupInfo.lpAttributeList);
        HeapFree(GetProcessHeap(), 0, m_shellStartupInfo.lpAttributeList);
        m_shellStartupInfo = {};
    }
    m_pid = 0;
    if (notifyExit) {
        m_exitNotified = true;
        Q_EMIT m_buffer.readyRead();
        Q_EMIT exited();
    }
    return hadConsole;
}

IPtyProcess::PtyType ConPtyProcess::type()
{
    return PtyType::ConPty;
}

QString ConPtyProcess::dumpDebugInfo()
{
#ifdef PTYQT_DEBUG
    return QStringLiteral("PID: %1, Type: %2, Cols: %3, Rows: %4").arg(m_pid).arg(type()).arg(m_size.first).arg(m_size.second);
#else
    return QStringLiteral("Nothing...");
#endif
}

QIODevice *ConPtyProcess::notifier()
{
    return &m_buffer;
}

QByteArray ConPtyProcess::readAll()
{
    QByteArray result;
    {
        QMutexLocker locker(&m_bufferMutex);
        result.swap(m_buffer.m_readBuffer);
    }
    return result;
}

qint64 ConPtyProcess::write(const char *data, int size)
{
    DWORD dwBytesWritten{};
    if (size <= 0 || m_hPipeOut == INVALID_HANDLE_VALUE) {
        return 0;
    }
    if (!WriteFile(m_hPipeOut, data, DWORD(size), &dwBytesWritten, nullptr)) {
        m_lastError = QStringLiteral("ConPTY: writing input failed: %1").arg(GetLastError());
        return -1;
    }
    return dwBytesWritten;
}

bool ConPtyProcess::isAvailable()
{
    // Probe the actual APIs: Server 2019 / Windows 10 1809 already provide ConPTY.
    // A hard-coded 18309 build check incorrectly rejected those supported systems.
    if (!m_winContext.init()) {
        m_lastError = QStringLiteral("ConPTY is unavailable on Windows %1. Requires Windows 10 1809 / Windows Server 2019 or newer. %2")
            .arg(QSysInfo::kernelVersion(), m_winContext.lastError());
        return false;
    }
    return true;
}

void ConPtyProcess::moveToThread(QThread *targetThread)
{
    // nothing for now...
}

int ConPtyProcess::processList() const
{
    return 0;
}

#include "moc_conptyprocess.cpp"
