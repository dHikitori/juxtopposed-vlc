// SPDX-License-Identifier: LGPL-2.1-or-later
/* Reuse the existing offline Lua runner with the four-provider fixture. */
#define main metadata_script_runner
#include "musicbrainz.c"
#undef main

int main(void)
{
    char name[] = "metadata-art-scripts";
    char provider[] = TOP_SRCDIR "/share/lua/meta/art/00_musicbrainz.lua";
    char fixture[] = TOP_SRCDIR "/test/modules/lua/art_outcomes.lua";
    char *arguments[] = {name, provider, fixture};
    return metadata_script_runner(3, arguments);
}
