// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
#include <iterator>
#include <vlc/vlc.h>
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    // Match the one initialized native instance in the source owner's actual
    // playback fixture. No add-on owner, player, or transport is constructed.
    const char* args[]={"--ignore-config","--quiet","--no-plugins-cache","--no-media-library","--no-metadata-network-access","--aout=adummy","--vout=vdummy","--text-renderer=tdummy"};
    auto* instance=libvlc_new(int(std::size(args)),args);
    if(!instance) return 2;
    libvlc_release(instance); return 0;
}
