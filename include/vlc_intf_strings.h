/*****************************************************************************
 * vlc_intf_strings.h : Strings for main interfaces
 *****************************************************************************
 * Copyright (C) 2003 VLC authors and VideoLAN
 *
 * Authors: Clément Stenac <zorglub@videolan.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifndef VLC_INTF_STRINGS_H
#define VLC_INTF_STRINGS_H 1

/**
 * \file
 * This file defines a number of strings used in user interfaces
 */

/* A helper macro that will expand to either of the arguments
   depending on platform. The arguments are supposed to be:
   1. dir: a string containing "directory"
   2. folder: a string with the same meaning but with directory
              substituted with "folder"
*/
#if defined( _WIN32 ) || defined(__APPLE__)
    #define I_DIR_OR_FOLDER( dir, folder ) folder
#else
    #define I_DIR_OR_FOLDER( dir, folder ) dir
#endif

/*************** Open dialogs **************/

#define I_OP_OPF        N_("&Open File...")
#define I_OP_ADVOP      N_("&Advanced Open...")
#define I_OP_OPDIR I_DIR_OR_FOLDER( N_("Open D&irectory..."), \
                                    N_("Open &Folder...") )
#define I_OP_SEL_FILES  N_("Select one or more files to open")
#define I_OP_SEL_DIR   I_DIR_OR_FOLDER( N_("Select Directory"), N_("Select Folder") )

/******************* Menus *****************/

#define I_MENU_INFO  N_("Media &Information")
#define I_MENU_CODECINFO  N_("&Codec Information")
#define I_MENU_MSG   N_("&Messages")
#define I_MENU_GOTOTIME N_("Jump to Specific &Time")
#define I_MENU_BOOKMARK N_("&Bookmarks")
#define I_MENU_VLM N_("&VLM Configuration")

#define I_MENU_ABOUT N_("&About")

/*************** Playlist *************/

#define I_PL_SAVE       N_("Save Play Queue to &File...")

#define I_PL_SEARCH     N_("Search")


/*************** Preferences *************/


/*************** Video filters **************/

#define I_LONGHELP N_("<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" /></head><body>" \
    "<h2>Welcome to Hakone Media Player Help</h2>" \
        "<h3>Documentation</h3>" \
            "<p>You can find news, downloads and information about Hakone on the <a href=\"https://hakone.org/\">Hakone website</a>.</p>" \
            "<p>Hakone is based on VLC media player, so most of VLC's documentation on VideoLAN's <a href=\"https://wiki.videolan.org\">wiki</a> website also applies.</p>" \
            "<p>For all the saving, converting, transcoding, encoding, muxing and streaming tasks, you should find useful information in VLC's <a href=\"https://wiki.videolan.org/Documentation:Streaming_HowTo\">Streaming Documentation</a>.</p>" \
            "<p>If you are unsure about terminology, please consult VLC's <a href=\"https://wiki.videolan.org/Knowledge_Base\">knowledge base</a>.</p>" \
        "<h3>Help</h3>" \
            "<p>To report a problem or ask a question, please use the <a href=\"https://github.com/hakone-org/hakone/issues\">Hakone issue tracker</a>. Please do not ask VideoLAN for help with Hakone.</p>" \
        "<h3>Contribute to the project</h3>" \
            "<p>Hakone is free software developed on <a href=\"https://github.com/hakone-org/hakone\">GitHub</a>. You can help by giving some of your time to test, report bugs, translate and code. And of course, you can <b>promote</b> Hakone Media Player.</p>" \
    "</body></html>")

#endif
