/*
    SPDX-FileCopyrightText: 2007-2008 Robert Knight <robertknight@gmail.com>
    SPDX-FileCopyrightText: 2020 Tomaz Canabrava <tcanabrava@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "FileFilterHotspot.h"
#include "TextFilePreview.h"
#include "FileMetadata.h"
#include "../SSHProcessInfo.h"
#ifdef Q_OS_WIN
#include "fileremote/FileRemoteProcess.h"
#include <windows.h>
#endif

#include <QAction>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QDrag>
#include <QDir>
#include <QFileInfo>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMimeDatabase>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QProcess>
#include <QPointer>
#include <QStandardPaths>
#include <QTimer>
#include <QToolTip>
#include <memory>

#include <KFileItem>
#include <KFileItemActions>
#include <KIO/ApplicationLauncherJob>
#include <KIO/OpenUrlJob>
#include <KIO/PreviewJob>

#include <KFileItemListProperties>
#include <KIO/JobUiDelegateFactory>
#include <KLocalizedString>
#include <KMessageBox>
#include <KShell>

#include "KonsoleSettings.h"
#include "konsoledebug.h"
#include "profile/Profile.h"
#include "session/SessionManager.h"
#include "terminalDisplay/TerminalDisplay.h"

using namespace Konsole;

#ifdef Q_OS_WIN
static QString posixShellQuote(QString value)
{
    value.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + value + QLatin1Char('\'');
}

static TextFilePreview::Loader sshFileLoader(Session *session)
{
    QPointer<Session> guardedSession(session);
    return [guardedSession](const QString &requestedPath, qint64 maximumBytes, bool showInfo, bool, QObject *receiver, TextFilePreview::Completion callback) {
        const auto fail = [&callback](const QString &message) {
            callback({{}, message});
        };
        if (!guardedSession || !guardedSession->isRemote()) {
            fail(QObject::tr("The SSH session is no longer active."));
            return;
        }

        ProcessInfo *processInfo = guardedSession->getProcessInfo();
        if (!processInfo) {
            fail(QObject::tr("Could not read the active SSH connection."));
            return;
        }
        processInfo->refreshArguments();
        SSHProcessInfo sshInfo(*processInfo);
        if (sshInfo.host().isEmpty()) {
            fail(QObject::tr("Could not determine the SSH host. Use a direct ssh host or user@host command."));
            return;
        }
        bool argumentsOk = false;
        const QVector<QString> originalArguments = processInfo->arguments(&argumentsOk);
        QString sshProgram = QStandardPaths::findExecutable(QStringLiteral("ssh"));
        if (sshProgram.isEmpty() && argumentsOk && !originalArguments.isEmpty()
            && QFileInfo(originalArguments.first()).isExecutable()) {
            sshProgram = originalArguments.first();
        }
        if (sshProgram.isEmpty()) {
            fail(QObject::tr("OpenSSH (ssh.exe) was not found in PATH."));
            return;
        }

        QString path = requestedPath;
        if (QDir::isRelativePath(path)) {
            QString remoteDirectory = guardedSession->reportedWorkingDirectory();
            if (remoteDirectory.isEmpty()) {
                remoteDirectory = guardedSession->property("sshPreviewDirectory").toString();
            }
            if (remoteDirectory.isEmpty()) {
                bool accepted = false;
                remoteDirectory = QInputDialog::getText(QApplication::activeWindow(), QObject::tr("Remote directory"),
                    QObject::tr("The SSH shell does not report its directory. Enter the absolute remote directory containing this file. Enable OSC 7 in the remote prompt to track directory changes automatically."),
                    QLineEdit::Normal, QStringLiteral("/"), &accepted);
                if (!accepted || !remoteDirectory.startsWith(QLatin1Char('/'))) {
                    fail(QObject::tr("An absolute remote directory is required.")); return;
                }
                guardedSession->setProperty("sshPreviewDirectory", remoteDirectory);
            }
            path = QDir::cleanPath(remoteDirectory + QLatin1Char('/') + path);
        }

        const qint64 limit = qBound(qint64(1), maximumBytes, qint64(256) * 1024 * 1024);
        const QString command = QStringLiteral(
            "LC_ALL=C; export LC_ALL; p=%1; "
            "if [ ! -f \"$p\" ]; then echo 'Remote path is not a regular file' >&2; exit 2; fi; "
            "size=$(wc -c < \"$p\" | tr -d ' '); "
            "printf '__KONSOLE_META__%s\\n' \"$size\"; "
            "printf 'File: %s\\n' \"$p\"; "
            "printf 'Created: '; stat -c '%w' -- \"$p\" 2>/dev/null; "
            "printf 'Modified: '; stat -c '%y' -- \"$p\" 2>/dev/null; "
            "printf 'Permissions: '; stat -c '%A (%a)' -- \"$p\" 2>/dev/null; "
            "printf 'Owner: '; stat -c '%U' -- \"$p\" 2>/dev/null; "
            "printf 'Group: '; stat -c '%G' -- \"$p\" 2>/dev/null; "
            "printf 'Type: '; file -b -- \"$p\" 2>/dev/null || printf 'file utility unavailable'; "
            "printf '\\n__KONSOLE_DATA__\\n'; "
            "if [ \"$size\" -le %2 ]; then head -c %2 -- \"$p\" | base64 | tr -d '\\r\\n'; fi; printf '\\n'")
                .arg(posixShellQuote(path), QString::number(limit));

        QStringList arguments{QStringLiteral("-T"), QStringLiteral("-o"), QStringLiteral("BatchMode=yes"),
                              QStringLiteral("-o"), QStringLiteral("ConnectTimeout=5")};
        QString destination;
        if (argumentsOk) {
            static const QString optionsWithValue = QStringLiteral("bcDeFIiJLlmOopRSWw");
            for (qsizetype i = 1; i < originalArguments.size(); ++i) {
                const QString &argument = originalArguments.at(i);
                if (argument == QStringLiteral("--")) {
                    if (i + 1 < originalArguments.size()) { destination = originalArguments.at(i + 1); }
                    break;
                }
                if (!argument.startsWith(QLatin1Char('-')) || argument == QStringLiteral("-")) {
                    destination = argument;
                    break;
                }
                const QChar option = argument.size() > 1 ? argument.at(1) : QLatin1Char('\0');
                if (!optionsWithValue.contains(option)) { continue; }
                QString value;
                if (argument.size() > 2) {
                    value = argument.mid(2);
                } else if (i + 1 < originalArguments.size()) {
                    value = originalArguments.at(++i);
                }
                if (option == QLatin1Char('F') || option == QLatin1Char('i') || option == QLatin1Char('J')
                    || option == QLatin1Char('S') || option == QLatin1Char('o') || option == QLatin1Char('p')
                    || option == QLatin1Char('l')) {
                    arguments << argument;
                    if (argument.size() == 2 && !value.isEmpty()) { arguments << value; }
                }
            }
        }
        if (destination.isEmpty()) {
            destination = sshInfo.host();
            if (!sshInfo.userName().isEmpty()) { destination.prepend(sshInfo.userName() + QLatin1Char('@')); }
            if (!sshInfo.port().isEmpty()) { arguments << QStringLiteral("-p") << sshInfo.port(); }
        }
        arguments << destination << command;

        auto *process = new QProcess(receiver);
        auto completed = std::make_shared<bool>(false);
        auto interactive = std::make_shared<bool>(false);
        auto finishWithError = [process, completed, callback](const QString &message) mutable {
            if (*completed) { return; }
            *completed = true;
            callback({{}, message});
            process->deleteLater();
        };
        QObject::connect(process, &QProcess::errorOccurred, process, [finishWithError](QProcess::ProcessError error) mutable {
            if (error == QProcess::FailedToStart) {
                finishWithError(QObject::tr("Could not start ssh.exe. Check the OpenSSH installation and PATH."));
            }
        });
        QObject::connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process,
            [process, completed, interactive, callback, showInfo, limit, sshProgram, arguments](int exitCode, QProcess::ExitStatus status) mutable {
                if (*completed) { return; }
                const QByteArray output = process->readAllStandardOutput();
                const QByteArray errorOutput = process->readAllStandardError();
                if (status == QProcess::NormalExit && exitCode != 0 && !*interactive
                    && (errorOutput.contains("Permission denied") || errorOutput.contains("Host key verification failed"))) {
                    *interactive = true;
                    QStringList retry = arguments;
                    retry.replaceInStrings(QStringLiteral("BatchMode=yes"), QStringLiteral("BatchMode=no"));
                    process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
                        args->flags |= CREATE_NEW_CONSOLE;
                    });
                    process->start(sshProgram, retry, QIODevice::ReadOnly);
                    return;
                }
                *completed = true;
                if (status != QProcess::NormalExit || exitCode != 0) {
                    const QString details = QString::fromLocal8Bit(errorOutput.left(8192)).trimmed();
                    callback({{}, details.isEmpty() ? QObject::tr("SSH file request failed (exit code %1).").arg(exitCode) : details});
                    process->deleteLater();
                    return;
                }
                const QByteArray metaMarker("__KONSOLE_META__");
                const QByteArray dataMarker("\n__KONSOLE_DATA__\n");
                const int metaStart = output.indexOf(metaMarker);
                const int dataStart = output.indexOf(dataMarker, metaStart < 0 ? 0 : metaStart + metaMarker.size());
                if (metaStart < 0 || dataStart < 0) {
                    callback({{}, QObject::tr("SSH returned an invalid file preview response.")});
                    process->deleteLater();
                    return;
                }
                const QByteArray sizeLine = output.mid(metaStart + metaMarker.size(), output.indexOf('\n', metaStart) - metaStart - metaMarker.size()).trimmed();
                bool sizeOk = false;
                const qint64 remoteSize = sizeLine.toLongLong(&sizeOk);
                QString metadata = QString::fromUtf8(output.mid(output.indexOf('\n', metaStart) + 1, dataStart - output.indexOf('\n', metaStart) - 1)).trimmed();
                if (sizeOk && remoteSize > limit) {
                    callback({{}, QObject::tr("This file exceeds the preview size limit. Increase the limit in Settings → Configure Konsole → Thumbnails / File previews."), false,
                              showInfo ? metadata : QString()});
                    process->deleteLater();
                    return;
                }
                const QByteArray encoded = output.mid(dataStart + dataMarker.size()).trimmed();
                const QByteArray data = QByteArray::fromBase64(encoded);
                auto content = TextPreviewContent::readBytes(data, limit);
                if (showInfo) { content.metadata = metadata; }
                if (content.binary && showInfo) {
                    content.error += QObject::tr("\n\nRemote file identification:\n%1").arg(metadata.section(QLatin1Char('\n'), -1));
                }
                callback(content);
                process->deleteLater();
            });
        QTimer::singleShot(120000, process, [process, finishWithError] {
            if (process->state() != QProcess::NotRunning) {
                process->kill();
                finishWithError(QObject::tr("Timed out while requesting a file over SSH."));
            }
        });
        process->start(sshProgram, arguments, QIODevice::ReadOnly);
    };
}
#endif

FileFilterHotSpot::FileFilterHotSpot(int startLine,
                                     int startColumn,
                                     int endLine,
                                     int endColumn,
                                     const QStringList &capturedTexts,
                                     const QString &filePath,
                                     Session *session)
    : RegExpFilterHotSpot(startLine, startColumn, endLine, endColumn, capturedTexts)
    , _filePath(filePath)
    , _session(session)
    , _thumbnailFinished(false)
{
    setType(File);
}

void FileFilterHotSpot::activate(QObject *)
{
    if (!_session) { // The Session is dead, nothing to do
        return;
    }

    if (isRemote() || (_session && _session->isRemote())) { return; }

    QString editorExecPath;
    int firstBlankIdx = -1;
    QString fullCmd;

    Profile::Ptr profile = SessionManager::instance()->sessionProfile(_session);
    const QString editorCmd = profile->textEditorCmd();
    if (!editorCmd.isEmpty()) {
        firstBlankIdx = editorCmd.indexOf(QLatin1Char(' '));
        if (firstBlankIdx != -1) {
            editorExecPath = QStandardPaths::findExecutable(editorCmd.mid(0, firstBlankIdx));
        } else { // No spaces, e.g. just a binary name "foo"
            editorExecPath = QStandardPaths::findExecutable(editorCmd);
        }
    }

    // Output of e.g.:
    // - grep with line numbers: "path/to/some/file:123:"
    //   grep with long lines e.g. "path/to/some/file:123:void blah" i.e. no space after 123:
    // - compiler errors with line/column numbers: "/path/to/file.cpp:123:123:" or "/path/to/file.cpp:123:123"
    // - ctest failing unit tests: "/path/to/file(204)"
    static const QRegularExpression re(QStringLiteral(R"foo([:\(](\d+)(?:\)\])?(?::(\d+):?|:[^\d]*)?$)foo"));
    const QRegularExpressionMatch match = re.match(_filePath);
    if (match.hasMatch()) {
        // The file path without the ":123" ... etc part
        const QString path = _filePath.mid(0, match.capturedStart(0));

        // TODO: show an error message to the user?
        if (editorExecPath.isEmpty()) { // Couldn't find the specified binary, fallback
            openWithSysDefaultApp(path);
            return;
        }
        if (firstBlankIdx != -1) {
            fullCmd = editorCmd;
            // Substitute e.g. "fooBinary" with full path, "/usr/bin/fooBinary"
            fullCmd.replace(0, firstBlankIdx, editorExecPath);

            fullCmd.replace(QLatin1String("PATH"), path);
            fullCmd.replace(QLatin1String("LINE"), match.captured(1));

            const QString col = match.captured(2);
            fullCmd.replace(QLatin1String("COLUMN"), !col.isEmpty() ? col : QLatin1String("0"));
        } else { // The editorCmd is just the binary name, no PATH, LINE or COLUMN
            // Add the "path" here, so it becomes "/path/to/fooBinary path"
            fullCmd += QLatin1Char(' ') + path;
        }

        openWithEditorFromProfile(fullCmd, path);
        return;
    }

    // There was no match, i.e. regular url "path/to/file"
    // Clean up the file path; the second branch in the regex is for "path/to/file:"
    QString path(_filePath);
    static const QRegularExpression cleanupRe(QStringLiteral(R"foo((:\d+[:]?|:)$)foo"), QRegularExpression::DontCaptureOption);
    path.remove(cleanupRe);
    if (!editorExecPath.isEmpty()) { // Use the editor from the profile settings
        const QString fCmd = editorExecPath + QLatin1Char(' ') + path;
        openWithEditorFromProfile(fCmd, path);
    } else { // Fallback
        openWithSysDefaultApp(path);
    }
}

void FileFilterHotSpot::openWithSysDefaultApp(const QString &filePath) const
{
    auto *job = new KIO::OpenUrlJob(QUrl::fromLocalFile(filePath));
    job->setUiDelegate(KIO::createDefaultJobUiDelegate(KJobUiDelegate::AutoHandlingEnabled, QApplication::activeWindow()));
    job->setRunExecutables(false); // Always open scripts, shell/python/perl... etc, as text
    job->start();
}

void FileFilterHotSpot::openWithEditorFromProfile(const QString &fullCmd, const QString &path) const
{
    // Here we are mostly interested in text-based files, e.g. if it's a
    // PDF we should let the system default app open it.
    QMimeDatabase mdb;
    const auto mimeType = mdb.mimeTypeForFile(path);
    qCDebug(KonsoleDebug) << "FileFilterHotSpot: mime type for" << path << ":" << mimeType;

    if (!mimeType.inherits(QStringLiteral("text/plain"))) {
        openWithSysDefaultApp(path);
        return;
    }

    qCDebug(KonsoleDebug) << "fullCmd:" << fullCmd;

    KService::Ptr service(new KService(QString(), fullCmd, QString()));

    // ApplicationLauncherJob is better at reporting errors to the user than
    // CommandLauncherJob; no need to call job->setUrls() because the url is
    // already part of fullCmd
    auto *job = new KIO::ApplicationLauncherJob(service);
    connect(job, &KJob::result, this, [this, path, job]() {
        if (job->error() != 0) {
            // TODO: use KMessageWidget (like the "terminal is read-only" message)
            KMessageBox::information(QApplication::activeWindow(),
                                     i18n("Could not open file with the text editor specified in the profile settings;\n"
                                          "it will be opened with the system default editor."));

            openWithSysDefaultApp(path);
        }
    });

    job->start();
}

FileFilterHotSpot::~FileFilterHotSpot() = default;

QList<QAction *> FileFilterHotSpot::actions()
{
    QAction *action = new QAction(i18n("Copy Full File Path"), this);
    action->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy-path")));
    const bool sshRemote = !isRemote() && _session && _session->isRemote();
    connect(action, &QAction::triggered, this, [this, sshRemote] {
        const QString path = TextPreviewContent::pathWithoutLocation(_filePath);
        QGuiApplication::clipboard()->setText(sshRemote ? path : FileMetadata::clipboardPath(path));
    });
    QList<QAction *> result{action};
#ifdef Q_OS_WIN
    if (sshRemote) {
        auto *directory = new QAction(i18n("Set Remote Preview Directory…"), this);
        connect(directory, &QAction::triggered, this, [this] {
            bool accepted = false;
            const QString path = QInputDialog::getText(QApplication::activeWindow(), i18n("Remote directory"),
                i18n("Absolute directory on the SSH host:"), QLineEdit::Normal,
                _session->property("sshPreviewDirectory").toString(), &accepted);
            if (accepted && path.startsWith(QLatin1Char('/'))) {
                _session->setProperty("sshPreviewDirectory", path);
            }
        });
        result << directory;
    }
#endif

    const QString path = TextPreviewContent::pathWithoutLocation(_filePath);
    if (isRemote() || sshRemote || QFileInfo(path).isFile()) {
        auto *copyContents = new QAction(i18n("Copy Entire File Contents"), this);
        copyContents->setIcon(QIcon::fromTheme(QStringLiteral("edit-copy")));
        connect(copyContents, &QAction::triggered, this, [this, path, sshRemote] {
            const qint64 maximum = qBound(1, KonsoleSettings::self()->textFilePreviewMaximumMiB(), 256) * qint64(1024 * 1024);
#ifdef Q_OS_WIN
            if (isRemote()) {
                const auto arguments = _session->arguments();
                TextFilePreview::copyFileContents(path, maximum, QApplication::activeWindow(),
                    [arguments](const QString &file, qint64 limit, bool info, bool textOnly, QObject *receiver, TextFilePreview::Completion callback) {
                        FileRemote::Process::requestFile(arguments, file, limit, info, textOnly, receiver, std::move(callback));
                    });
                return;
            }
            if (sshRemote) {
                TextFilePreview::copyFileContents(path, maximum, QApplication::activeWindow(), sshFileLoader(_session));
                return;
            }
#endif
            TextFilePreview::copyFileContents(path, maximum, QApplication::activeWindow());
        });
        result << copyContents;
    }
    return result;
}

QList<QAction *> FileFilterHotSpot::setupMenu(QMenu *menu)
{
    if (isRemote() || (_session && _session->isRemote())) { return {}; }
    if (!_menuActions)
        _menuActions = new KFileItemActions;
    const QList<QAction *> currentActions = menu->actions();

    const KFileItem fileItem(QUrl::fromLocalFile(TextPreviewContent::pathWithoutLocation(_filePath)));
    const KFileItemList itemList({fileItem});
    const KFileItemListProperties itemProperties(itemList);
    _menuActions->setParent(this);
    _menuActions->setItemListProperties(itemProperties);

    const QList<QAction *> actionList = menu->actions();
    _menuActions->insertOpenWithActionsTo(!actionList.isEmpty() ? actionList.at(0) : nullptr, menu, QStringList());

    QList<QAction *> addedActions = menu->actions();
    // addedActions will only contain the open-with actions
    for (auto *act : currentActions) {
        addedActions.removeOne(act);
    }

    return addedActions;
}

// Static variables for the HotSpot
bool FileFilterHotSpot::_canGenerateThumbnail = false;
QPointer<KIO::PreviewJob> FileFilterHotSpot::_previewJob;

void FileFilterHotSpot::requestThumbnail(Qt::KeyboardModifiers modifiers, const QPoint &pos)
{
    if (isRemote() || (_session && _session->isRemote()) || KonsoleSettings::self()->enableTextFilePreviews() || !KonsoleSettings::self()->enableThumbnails()) {
        return;
    }

    _canGenerateThumbnail = true;
    _eventModifiers = modifiers;
    _eventPos = pos;

    // Defer the real creation of the thumbnail by a few msec.
    QTimer::singleShot(250, this, [this] {
        thumbnailRequested();
    });
}

void FileFilterHotSpot::stopThumbnailGeneration()
{
    _canGenerateThumbnail = false;
    if (_previewJob != nullptr) {
        _previewJob->deleteLater();
        QToolTip::hideText();
    }
}

void FileFilterHotSpot::showThumbnail(const KFileItem &item, const QPixmap &preview)
{
    if (!_canGenerateThumbnail) {
        return;
    }
    _thumbnailFinished = true;
    Q_UNUSED(item)
    QByteArray data;
    QBuffer buffer(&data);
    preview.save(&buffer, "PNG", 100);

    const auto tooltipString = QStringLiteral("<img src='data:image/png;base64, %0'>").arg(QString::fromLocal8Bit(data.toBase64()));

    QToolTip::showText(_thumbnailPos, tooltipString, qApp->focusWidget());
}

void FileFilterHotSpot::thumbnailRequested()
{
    if (!_canGenerateThumbnail) {
        return;
    }

    auto *settings = KonsoleSettings::self();

    Qt::KeyboardModifiers modifiers = settings->thumbnailCtrl() ? Qt::ControlModifier : Qt::NoModifier;
    modifiers |= settings->thumbnailAlt() ? Qt::AltModifier : Qt::NoModifier;
    modifiers |= settings->thumbnailShift() ? Qt::ShiftModifier : Qt::NoModifier;

    if (_eventModifiers != modifiers) {
        return;
    }

    // TODO: check (async) if file actually exists, to prevent a warning from KIO::PreviewJob

    _thumbnailPos = QPoint(_eventPos.x() + 100, _eventPos.y() - settings->thumbnailSize() / 2);

    const int size = KonsoleSettings::thumbnailSize();
    if (_previewJob != nullptr) {
        _previewJob->deleteLater();
    }

    _thumbnailFinished = false;

    // Show a "Loading" if Preview takes a long time.
    QTimer::singleShot(10, this, [this] {
        if (_previewJob == nullptr) {
            return;
        }
        if (!_thumbnailFinished) {
            QToolTip::showText(_thumbnailPos, i18n("Generating Thumbnail"), qApp->focusWidget());
        }
    });

    // preview job is auto-started & auto-deleted
    _previewJob = new KIO::PreviewJob(KFileItemList({fileItem()}), QSize(size, size));
    connect(_previewJob, &KIO::PreviewJob::gotPreview, this, &FileFilterHotSpot::showThumbnail);
    connect(_previewJob, &KIO::PreviewJob::failed, this, [] {
        qCDebug(KonsoleDebug) << "Error generating the preview" << _previewJob->errorString();
        QToolTip::hideText();
    });
}

KFileItem FileFilterHotSpot::fileItem() const
{
    return KFileItem(QUrl::fromLocalFile(TextPreviewContent::pathWithoutLocation(_filePath)));
}

void FileFilterHotSpot::mouseEnterEvent(TerminalDisplay *td, QMouseEvent *ev)
{
    HotSpot::mouseEnterEvent(td, ev);
    requestThumbnail(ev->modifiers(), ev->globalPosition().toPoint());
    mouseMoveEvent(td, ev);
}

void FileFilterHotSpot::mouseMoveEvent(TerminalDisplay *td, QMouseEvent *ev)
{
    auto *settings = KonsoleSettings::self();
    TextPreviewOptions options;
    options.enabled = settings->enableTextFilePreviews();
    options.showFileInfo = settings->textFilePreviewShowFileInfo();
    options.delayMs = settings->textFilePreviewDelay();
    options.leaveDelayMs = settings->textFilePreviewLeaveDelay();
    options.width = settings->textFilePreviewWidth();
    options.height = settings->textFilePreviewHeight();
    options.maximumMiB = settings->textFilePreviewMaximumMiB();
    _textPreview = TextFilePreview::forWidget(td);
#ifdef Q_OS_WIN
    if (isRemote()) {
        const auto arguments = _session->arguments();
        _textPreview->hoverFile(_filePath, ev->globalPosition().toPoint(), options,
            [arguments](const QString &path, qint64 limit, bool info, bool textOnly, QObject *receiver, TextFilePreview::Completion callback) {
                FileRemote::Process::requestFile(arguments, path, limit, info, textOnly, receiver, std::move(callback));
            }, arguments.join(QLatin1Char(' ')));
        return;
    }
    if (_session && _session->isRemote()) {
        ProcessInfo *processInfo = _session->getProcessInfo();
        processInfo->refreshArguments();
        const SSHProcessInfo sshInfo(*processInfo);
        _textPreview->hoverFile(_filePath, ev->globalPosition().toPoint(), options, sshFileLoader(_session),
                                QStringLiteral("ssh:%1").arg(sshInfo.host()));
        return;
    }
#endif
    _textPreview->hoverFile(_filePath, ev->globalPosition().toPoint(), options);
}

void FileFilterHotSpot::mouseLeaveEvent(TerminalDisplay *td, QMouseEvent *ev)
{
    HotSpot::mouseLeaveEvent(td, ev);
    stopThumbnailGeneration();
    if (_textPreview) {
        _textPreview->leavePath();
    }
}

void FileFilterHotSpot::keyPressEvent(Konsole::TerminalDisplay *td, QKeyEvent *ev)
{
    HotSpot::keyPressEvent(td, ev);
    requestThumbnail(ev->modifiers(), QCursor::pos());
}

bool FileFilterHotSpot::hasDragOperation() const
{
    return !isRemote() && !(_session && _session->isRemote());
}

void FileFilterHotSpot::startDrag(TerminalDisplay *td, QPoint dragPosition)
{
    auto *drag = new QDrag(this);
    auto *mimeData = new QMimeData();
    mimeData->setUrls({QUrl::fromLocalFile(_filePath)});

    drag->setMimeData(mimeData);
    setDragPixmap(drag, td, dragPosition);
    drag->exec(Qt::CopyAction);
}

#include "moc_FileFilterHotspot.cpp"

bool FileFilterHotSpot::isRemote() const
{
    return _session && _session->arguments().contains(QStringLiteral("--konsole-file-remote"));
}
