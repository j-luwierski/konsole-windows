/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KONSOLE_TEXTFILEPREVIEW_H
#define KONSOLE_TEXTFILEPREVIEW_H

#include "TextPreviewContent.h"
#include <functional>
#include <QObject>
#include <QPoint>
#include <QTimer>
#include <QWidget>

class QFrame;
class QPlainTextEdit;
namespace Konsole
{
struct TextPreviewOptions {
    bool enabled = true;
    bool showFileInfo = true;
    int delayMs = 2000;
    int leaveDelayMs = 250;
    int width = 640;
    int height = 420;
    int maximumMiB = 16;
};


class TextFilePreview : public QObject
{
    Q_OBJECT
public:
    using Completion = std::function<void(const TextPreviewContent &)>;
    using Loader = std::function<void(const QString &, qint64, bool, bool, QObject *, Completion)>;
    explicit TextFilePreview(QWidget *owner);
    static TextFilePreview *forWidget(QWidget *owner);
    static void copyFileContents(const QString &path, qint64 maximumBytes, QWidget *messageParent = nullptr, Loader loader = {});
    void hoverFile(const QString &path, const QPoint &globalPosition, const TextPreviewOptions &options, Loader loader = {}, const QString &sourceId = {});
    void leavePath();
    void dismiss();
    QWidget *popup() const;
    QPlainTextEdit *editor() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void load();
    void showContent(const TextPreviewContent &content);
    QWidget *_owner;
    QFrame *_popup;
    QPlainTextEdit *_editor;
    QTimer _showTimer;
    QTimer _leaveTimer;
    QString _path, _sourceId;
    Loader _loader;
    QPoint _anchor;
    TextPreviewOptions _options;
    quint64 _generation = 0;
    bool _hoveringPath = false;
    bool _enteredPopup = false;
};
}
#endif
