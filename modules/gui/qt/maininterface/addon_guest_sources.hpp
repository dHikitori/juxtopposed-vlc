// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "qt.hpp"

class MainCtx;
namespace vlc::addons { class AddonGuestProjections; class AddonHostServices; class AddonMediaSiblings; }
class SeriesModel;

// Connects the guest projections to the app's queue, media library and
// player: queue reads and removals, library reads and the playing file's
// fingerprint. The library source is omitted without a library.
vlc::addons::AddonGuestProjections* mountAddonGuestSources(qt_intf_t*, MainCtx*, vlc::addons::AddonHostServices*,
                                                           QObject* parent);

// Connects media.siblings (add-on player panels) to the series model that
// finds the other episodes of the playing media.
vlc::addons::AddonMediaSiblings* mountAddonMediaSiblings(vlc::addons::AddonHostServices*, SeriesModel*, QObject* parent);
