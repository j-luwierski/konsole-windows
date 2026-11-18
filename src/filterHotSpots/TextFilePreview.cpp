/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "TextFilePreview.h"

#include <QApplication>
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

TextPreviewContent TextPreviewContent::read(const QString &path, qint64 maximumBytes)
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
        if (!_hoveringPath && !_popup->geometry().contains(QCursor::pos())) {
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

QWidget *TextFilePreview::popup() const { return _popup; }
QPlainTextEdit *TextFilePreview::editor() const { return _editor; }

void TextFilePreview::hoverFile(const QString &path, const QPoint &position, const TextPreviewOptions &options)
{
    if (!options.enabled) {
        dismiss();
        return;
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
    const qint64 limit = qBound(1, _options.maximumMiB, 256) * qint64(1024 * 1024);
    auto *watcher = new QFutureWatcher<TextPreviewContent>(this);
    connect(watcher, &QFutureWatcher<TextPreviewContent>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (!result.binary && generation == _generation && _hoveringPath && QCursor::pos() == _anchor && _owner->isVisible()) {
            showContent(result);
        }
    });
    watcher->setFuture(QtConcurrent::run([path, limit] { return TextPreviewContent::read(path, limit); }));
}

void TextFilePreview::showContent(const TextPreviewContent &content)
{
    QToolTip::hideText();
    _editor->setPlainText(content.error.isEmpty() ? content.text : content.error);
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
        _leaveTimer.stop();
    } else if (_popup->isVisible() && (event->type() == QEvent::MouseMove || event->type() == QEvent::Leave)) {
        if (_popup->geometry().contains(QCursor::pos())) {
            _leaveTimer.stop();
        } else if (!_hoveringPath && !_leaveTimer.isActive()) {
            _leaveTimer.start(qBound(100, _options.leaveDelayMs, 2000));
        }
    }
    if (event->type() == QEvent::ApplicationDeactivate
        || (event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape)) {
        dismiss();
    }
    return QObject::eventFilter(watched, event);
}
