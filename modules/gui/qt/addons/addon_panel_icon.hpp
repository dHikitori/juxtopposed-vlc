// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QString>

namespace vlc::addons::panelIcon {
// A package icon for a layout button: a single-color SVG with a 24 by 24
// view box and only plain shapes. The result is the inner markup in the form
// that RedesignIcon draws, with each paint color replaced by COLOR, so the
// app colors it for the theme. Scripts, styles, links, text, images and
// external references are refused. An empty result means "use the generic
// icon"; the package still installs.
constexpr qsizetype MaximumBytes = 16 * 1024;
QString markup(const QByteArray& svg);
}
