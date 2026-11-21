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

void TextFilePreview::copyFileContents(const QString &path, qint64 maximumBytes, QWidget *messageParent, Loader loader)
{
    // A hotspot may disappear as soon as its context menu closes. The operation
    // belongs to the application so that copying still completes afterwards.
    const QPointer<QWidget> window(messageParent);
    const auto completed = [window](const TextPreviewContent &result) {
        if (!result.error.isEmpty()) {
            QMessageBox::warning(window, tr("Cannot copy file contents"), result.error); return;
        }
        QGuiApplication::clipboard()->setText(result.text);
    };
    if (loader) { loader(path, maximumBytes, false, true, qApp, completed); return; }
    auto *watcher = new QFutureWatcher<TextPreviewContent>(qApp);
    connect(watcher, &QFutureWatcher<TextPreviewContent>::finished, qApp, [watcher, completed] {
        const auto result = watcher->result(); watcher->deleteLater(); completed(result);
    });
    watcher->setFuture(QtConcurrent::run([path, maximumBytes] { return TextPreviewContent::readText(path, maximumBytes); }));
}

QWidget *TextFilePreview::popup() const { return _popup; }
QPlainTextEdit *TextFilePreview::editor() const { return _editor; }

void TextFilePreview::hoverFile(const QString &path, const QPoint &position, const TextPreviewOptions &options, Loader loader, const QString &sourceId)
{
    if (!options.enabled) {
        dismiss();
        return;
    }
    if (_popup->isVisible() && _enteredPopup && !_popup->geometry().contains(position)) {
        dismiss();
    }
    const bool wasHoveringPath = _hoveringPath;
    _leaveTimer.stop();
    _hoveringPath = true;
    _options = options;
    const QString filePath = TextPreviewContent::pathWithoutLocation(path);
    if (filePath == _path && sourceId == _sourceId && _popup->isVisible()) {
        return;
    }
    if (filePath != _path || sourceId != _sourceId || position != _anchor || !wasHoveringPath) {
        ++_generation;
        _popup->hide();
        _path = filePath; _sourceId = sourceId; _loader = std::move(loader);
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
    const auto completed = [this, generation](const TextPreviewContent &result) {
        if (generation == _generation && _hoveringPath && QCursor::pos() == _anchor && _owner->isVisible()) {
            showContent(result);
        }
    };
    if (_loader) { _loader(path, limit, showFileInfo, false, this, completed); return; }
    auto *watcher = new QFutureWatcher<TextPreviewContent>(this);
    connect(watcher, &QFutureWatcher<TextPreviewContent>::finished, this, [watcher, completed] {
        const auto result = watcher->result(); watcher->deleteLater(); completed(result);
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
