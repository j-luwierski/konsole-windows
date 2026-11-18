/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "TextFilePreview.h"
#include "FileMetadata.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QClipboard>
#include <QCursor>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QFutureWatcher>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScreen>
#include <QShortcut>
#include <QStringConverter>
#include <QVBoxLayout>
#include <QToolTip>
#include <QtConcurrentRun>

using namespace Konsole;

QString TextPreviewContent::windowsPathPattern()
{
    return QStringLiteral(R"RX((?:[A-Za-z]:[\\/]|\\\\)[^\s"'<>|]+|[.\p{L}\w~-]+(?:\\[^\s"'<>|]+)+|)RX");
}

QString TextPreviewContent::pathWithoutLocation(QString path)
{
    if (path.size() >= 2 && ((path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
                            || (path.front() == QLatin1Char('\'') && path.back() == QLatin1Char('\'')))) {
        path = path.mid(1, path.size() - 2);
    }
    static const QRegularExpression suffix(QStringLiteral(R"((:\d+(?::\d+)?:?|\(\d+(?:,\d+)?\)\]?)$)"));
    path.remove(suffix);
    if (path.size() >= 2 && ((path.front() == QLatin1Char('"') && path.back() == QLatin1Char('"'))
                            || (path.front() == QLatin1Char('\'') && path.back() == QLatin1Char('\'')))) {
        path = path.mid(1, path.size() - 2);
    }
    return path;
}

QString TextPreviewContent::fileCommand()
{
#ifdef Q_OS_WIN
    const QString bundled = QDir::cleanPath(QCoreApplication::applicationDirPath() + QStringLiteral("/../file/file.exe"));
    if (QFileInfo::exists(bundled)) { return bundled; }
#endif
    return QStandardPaths::findExecutable(QStringLiteral("file"));
}

TextPreviewContent TextPreviewContent::read(const QString &path, qint64 maximumBytes, bool showFileInfo, const QString &fileExecutable)
{
    auto content = readText(path, maximumBytes);
    content.metadata = FileMetadata::describe(path);
    if (!showFileInfo || content.error.isEmpty()) {
        return content;
    }
    const QFileInfo info(path);
    QString description = QObject::tr("Text preview unavailable: %1\n\nFile: %2\nSize: %3 bytes\nMIME: %4\n\nfile:\n")
                              .arg(content.error, info.fileName()).arg(info.size()).arg(QMimeDatabase().mimeTypeForFile(info).name());
    const QString program = fileExecutable.isEmpty() ? fileCommand() : fileExecutable;
    if (program.isEmpty()) {
        description += QObject::tr("The file identification tool is unavailable.");
    } else {
        QProcess process;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process.setProcessEnvironment(environment);
        QStringList arguments{QStringLiteral("--brief")};
        const QString magic = QFileInfo(program).dir().filePath(QStringLiteral("magic.mgc"));
        if (QFileInfo::exists(magic)) { arguments << QStringLiteral("--magic-file") << magic; }
        arguments << QStringLiteral("--") << info.absoluteFilePath();
        process.start(program, arguments, QIODevice::ReadOnly);
        if (!process.waitForStarted(1000) || !process.waitForFinished(5000)) {
            process.kill();
            process.waitForFinished(1000);
            description += QObject::tr("file could not complete: %1").arg(process.errorString());
        } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            QByteArray errorOutput = process.readAllStandardError();
            errorOutput += process.readAllStandardOutput();
            description += QObject::tr("file failed: %1").arg(QString::fromLocal8Bit(errorOutput.left(32768)));
        } else {
            description += QString::fromUtf8(process.readAllStandardOutput().left(32768)).trimmed();
        }
    }
    return {description, {}, false, content.metadata};
}

TextPreviewContent TextPreviewContent::readText(const QString &path, qint64 maximumBytes)
{
    const QFileInfo info(path);
    if (!info.isFile()) {
        return {{}, QObject::tr("The path is not a regular local file.")};
    }
    if (info.size() > maximumBytes) {
        return {{}, QObject::tr("This file exceeds the preview size limit. Increase the limit in Settings → Configure Konsole → Thumbnails / File previews.")};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {{}, QObject::tr("Cannot read the file: %1").arg(file.errorString())};
    }
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError) {
        return {{}, QObject::tr("Cannot read the file: %1").arg(file.errorString())};
    }
    if (bytes.size() > maximumBytes) {
        return {{}, QObject::tr("The file grew beyond the preview size limit.")};
    }
    const auto detected = QStringConverter::encodingForData(bytes);
    const auto encoding = detected.value_or(QStringConverter::Utf8);
    if (!detected && bytes.contains('\0')) {
        return {{}, QObject::tr("This file contains binary data and cannot be previewed as text."), true};
    }
    QStringDecoder decoder(encoding);
    QString text = decoder(bytes);
    if (decoder.hasError() && !detected) {
        QStringDecoder local(QStringConverter::System);
        text = local(bytes);
        if (local.hasError()) {
            return {{}, QObject::tr("The text encoding could not be decoded. Use UTF-8 or Unicode with a byte-order mark.")};
        }
    } else if (decoder.hasError()) {
        return {{}, QObject::tr("The file contains invalid text encoding.")};
    }
    int controls = 0;
    for (const auto ch : text) {
        if ((ch.unicode() < 32 && ch != QLatin1Char('\n') && ch != QLatin1Char('\r') && ch != QLatin1Char('\t') && ch != QLatin1Char('\f'))
            || ch == QChar(0x7f)) {
            ++controls;
        }
    }
    if (text.contains(QChar(0)) || (controls > 0 && controls * qint64(100) > text.size())) {
        return {{}, QObject::tr("This file contains binary data and cannot be previewed as text."), true};
    }
    return {text, {}};
}

TextFilePreview::TextFilePreview(QWidget *owner)
    : QObject(owner)
    , _owner(owner)
    , _popup(new QFrame(owner, Qt::Tool | Qt::FramelessWindowHint))
    , _editor(new QPlainTextEdit(_popup))
{
    setObjectName(QStringLiteral("konsoleTextFilePreview"));
    _popup->setObjectName(QStringLiteral("textFilePreviewPopup"));
    _popup->setAttribute(Qt::WA_ShowWithoutActivating);
    _popup->setFrameStyle(QFrame::NoFrame);
    auto *layout = new QVBoxLayout(_popup);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(_editor);
    _editor->setReadOnly(true);
    _editor->setFrameStyle(QFrame::NoFrame);
    _editor->setLineWrapMode(QPlainTextEdit::NoWrap);
    _editor->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    _editor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    auto *copy = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C), _editor);
    connect(copy, &QShortcut::activated, _editor, &QPlainTextEdit::copy);
    _showTimer.setSingleShot(true);
    _leaveTimer.setSingleShot(true);
    connect(&_showTimer, &QTimer::timeout, this, &TextFilePreview::load);
    connect(&_leaveTimer, &QTimer::timeout, this, [this] {
        if ((_enteredPopup || !_hoveringPath) && !_popup->geometry().contains(QCursor::pos())) {
            dismiss();
        }
    });
    qApp->installEventFilter(this);
}

TextFilePreview *TextFilePreview::forWidget(QWidget *owner)
{
    auto *preview = owner->findChild<TextFilePreview *>(QStringLiteral("konsoleTextFilePreview"), Qt::FindDirectChildrenOnly);
    return preview ? preview : new TextFilePreview(owner);
}

void TextFilePreview::copyFileContents(const QString &path, qint64 maximumBytes, QWidget *messageParent)
{
    // A hotspot may disappear as soon as its context menu closes. The operation
    // belongs to the application so that copying still completes afterwards.
    auto *watcher = new QFutureWatcher<TextPreviewContent>(qApp);
    const QPointer<QWidget> window(messageParent);
    connect(watcher, &QFutureWatcher<TextPreviewContent>::finished, qApp, [watcher, window] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.error.isEmpty()) {
            QMessageBox::warning(window, tr("Cannot copy file contents"), result.error);
            return;
        }
        QGuiApplication::clipboard()->setText(result.text);
    });
    watcher->setFuture(QtConcurrent::run([path, maximumBytes] { return TextPreviewContent::readText(path, maximumBytes); }));
}

QWidget *TextFilePreview::popup() const { return _popup; }
QPlainTextEdit *TextFilePreview::editor() const { return _editor; }

void TextFilePreview::hoverFile(const QString &path, const QPoint &position, const TextPreviewOptions &options)
{
    if (!options.enabled) {
        dismiss();
        return;
    }
    if (_popup->isVisible() && _enteredPopup && !_popup->geometry().contains(position)) {
        dismiss();
    }
    _leaveTimer.stop();
    _hoveringPath = true;
    _options = options;
    const QString filePath = TextPreviewContent::pathWithoutLocation(path);
    if (filePath == _path && _popup->isVisible()) {
        return;
    }
    if (filePath != _path || position != _anchor || !_showTimer.isActive()) {
        ++_generation;
        _popup->hide();
        _path = filePath;
        _anchor = position;
        _showTimer.start(qBound(100, options.delayMs, 30000));
    }
}

void TextFilePreview::leavePath()
{
    _hoveringPath = false;
    _showTimer.stop();
    ++_generation;
    if (_popup->isVisible()) {
        _leaveTimer.start(qBound(100, _options.leaveDelayMs, 2000));
    }
}

void TextFilePreview::dismiss()
{
    ++_generation;
    _showTimer.stop();
    _leaveTimer.stop();
    _hoveringPath = false;
    _enteredPopup = false;
    _popup->hide();
    _path.clear();
}

void TextFilePreview::load()
{
    if (!_hoveringPath || !_owner->isVisible()) {
        return;
    }
    if (QCursor::pos() != _anchor) {
        _anchor = QCursor::pos();
        _showTimer.start(qBound(100, _options.delayMs, 30000));
        return;
    }
    const auto generation = ++_generation;
    const auto path = _path;
    const bool showFileInfo = _options.showFileInfo;
    const qint64 limit = qBound(1, _options.maximumMiB, 256) * qint64(1024 * 1024);
    auto *watcher = new QFutureWatcher<TextPreviewContent>(this);
    connect(watcher, &QFutureWatcher<TextPreviewContent>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if ((!result.binary || !result.metadata.isEmpty()) && generation == _generation && _hoveringPath && QCursor::pos() == _anchor && _owner->isVisible()) {
            showContent(result);
        }
    });
    watcher->setFuture(QtConcurrent::run([path, limit, showFileInfo] { return TextPreviewContent::read(path, limit, showFileInfo); }));
}

void TextFilePreview::showContent(const TextPreviewContent &content)
{
    QToolTip::hideText();
    const QString body = content.error.isEmpty() ? content.text : content.error;
    _editor->setPlainText(content.metadata.isEmpty() ? body : content.metadata + QStringLiteral("\n\n--- File contents / information ---\n\n") + body);
    _editor->setAccessibleName(tr("Text preview: %1").arg(_path));
    _popup->setWindowTitle(_path);
    auto *screen = QGuiApplication::screenAt(_anchor);
    if (!screen) { screen = _owner->screen(); }
    const QRect available = screen->availableGeometry();
    const QSize size(qMin(qBound(320, _options.width, 1600), available.width()),
                     qMin(qBound(160, _options.height, 1200), available.height()));
    QPoint position = _anchor + QPoint(12, 16);
    position.setX(qBound(available.left(), position.x(), available.right() - size.width() + 1));
    position.setY(qBound(available.top(), position.y(), available.bottom() - size.height() + 1));
    _enteredPopup = false;
    _popup->setGeometry(QRect(position, size));
    _popup->show();
    _popup->raise();
}

bool TextFilePreview::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == _owner && (event->type() == QEvent::Hide || event->type() == QEvent::Close)) {
        dismiss();
    } else if (watched == _owner && event->type() == QEvent::Leave) {
        leavePath();
    } else if (watched == _popup && event->type() == QEvent::Enter) {
        _hoveringPath = false;
        _enteredPopup = true;
        _leaveTimer.stop();
    } else if (_popup->isVisible() && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave)) {
        if (_popup->geometry().contains(QCursor::pos())) {
            _enteredPopup = true;
            _hoveringPath = false;
            _leaveTimer.stop();
        } else if ((_enteredPopup || !_hoveringPath) && !_leaveTimer.isActive()) {
            _leaveTimer.start(qBound(100, _options.leaveDelayMs, 2000));
        }
    }
    if (event->type() == QEvent::ApplicationDeactivate
        || (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)) {
        dismiss();
    }
    return QObject::eventFilter(watched, event);
}
