/* SPDX-License-Identifier: MIT */
#ifndef KONSOLE_SELECTIONCLIPBOARD_H
#define KONSOLE_SELECTIONCLIPBOARD_H

#include <QString>

namespace Konsole
{
// Windows has no native PRIMARY selection. Keep a separate, process-wide
// selection shared by terminal views, without modifying the system clipboard.
class SelectionClipboard
{
public:
    static void setText(const QString &text)
    {
        if (!text.isEmpty()) {
            storage() = text;
        }
    }
    static QString text()
    {
        return storage();
    }

private:
    static QString &storage()
    {
        static QString selection;
        return selection;
    }
};
}
#endif
