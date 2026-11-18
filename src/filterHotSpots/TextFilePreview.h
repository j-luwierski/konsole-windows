/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef KONSOLE_TEXTFILEPREVIEW_H
#define KONSOLE_TEXTFILEPREVIEW_H

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
struct TextPreviewContent {
    QString text;
    QString error;
    bool binary = false;
    QString metadata;
    static TextPreviewContent read(const QString &path, qint64 maximumBytes, bool showFileInfo = false, const QString &fileExecutable = {});
    static TextPreviewContent readText(const QString &path, qint64 maximumBytes);
    static QString fileCommand();
    static QString pathWithoutLocation(QString path);
    static QString windowsPathPattern();
};

class TextFilePreview : public QObject
{
    Q_OBJECT
public:
    explicit TextFilePreview(QWidget *owner);
    static TextFilePreview *forWidget(QWidget *owner);
    static void copyFileContents(const QString &path, qint64 maximumBytes, QWidget *messageParent = nullptr);
    void hoverFile(const QString &path, const QPoint &globalPosition, const TextPreviewOptions &options);
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
    QString _path;
    QPoint _anchor;
    TextPreviewOptions _options;
    quint64 _generation = 0;
    bool _hoveringPath = false;
    bool _enteredPopup = false;
};
}
#endif
