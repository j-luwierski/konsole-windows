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

#define READ_INTERVAL_MSEC 500

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
        m_lastError = m_winContext.lastError();
        return false;
    }

    // already running
    if (m_ptyHandler != INVALID_HANDLE_VALUE)
        return false;

    m_shellPath = QStandardPaths::findExecutable(shellPath);
    if (m_shellPath.isEmpty()) {
        m_lastError = QStringLiteral("ConPTY: executable not found: %1").arg(shellPath);
        return false;
    }
    if (cols <= 0 || rows <= 0) {
        m_lastError = QStringLiteral("ConPTY: terminal dimensions must be positive");
        return false;
    }
    m_size = {cols, rows};
    m_aboutToDestruct = false;
    m_exitCode = -1;
    m_exitNotified = false;
    auto env = WindowsProcessUtils::environmentBlock(environment);
    auto cmdArg = WindowsProcessUtils::commandLine(m_shellPath, arguments).toStdWString();
    const auto nativeProgram = QDir::toNativeSeparators(m_shellPath).toStdWString();
    const auto nativeDirectory = QDir::toNativeSeparators(workingDir).toStdWString();

    HRESULT hr{E_UNEXPECTED};

    //  Create the Pseudo Console and pipes to it
    hr = createPseudoConsoleAndPipes(&m_ptyHandler, &m_hPipeIn, &m_hPipeOut, cols, rows);

    if (S_OK != hr) {
        m_lastError = QStringLiteral("ConPTY: creating console failed: %1").arg(quint32(hr));
        kill();
        return false;
    }

    // Initialize the necessary startup info struct
    if (S_OK != initializeStartupInfoAttachedToPseudoConsole(&m_shellStartupInfo, m_ptyHandler)) {
        m_lastError = QStringLiteral("ConPTY: initializing process attributes failed: %1").arg(GetLastError());
        kill();
        return false;
    }

    // Launch ping to Q_EMIT some text back via the pipe
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

    if (hr != S_OK) {
        m_lastError = QStringLiteral("ConPTY: cannot create process: %1").arg(hr);
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
        DWORD code = 0;
        if (GetExitCodeProcess(m_shellProcessInformation.hProcess, &code) && code != STILL_ACTIVE) {
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
    if (!GetExitCodeProcess(m_shellProcessInformation.hProcess, &code) || code == STILL_ACTIVE) {
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
    m_aboutToDestruct = true;
    m_exitPoll.stop();
    disconnect(&m_exitPoll, nullptr, this, nullptr);
    const bool hadConsole = m_ptyHandler != INVALID_HANDLE_VALUE && m_ptyHandler != nullptr;
    if (hadConsole) {
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
    // #ifdef TOO_OLD_WINSDK
    // return false; // very importnant! ConPty can be built, but it doesn't work
    // if built with old sdk and Win10 < 1903
    // #endif

    qint32 buildNumber = QSysInfo::kernelVersion().split(QLatin1String(".")).last().toInt();
    if (buildNumber < CONPTY_MINIMAL_WINDOWS_VERSION)
        return false;
    return m_winContext.init();
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
