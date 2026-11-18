/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <QByteArray>
class QWidget;
namespace Konsole::FileRemote {
// Returns compact connection JSON, or an empty byte array on cancellation.
QByteArray configureConnection(QWidget *parent, bool connectNow);
}
