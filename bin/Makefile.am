# Building hakone
#
if BUILD_VLC
if HAVE_OSX
bin_PROGRAMS = hakone-osx
noinst_PROGRAMS = hakone-osx-static
else
bin_PROGRAMS = hakone
noinst_PROGRAMS = hakone-static
endif
endif
EXTRA_DIST = vlc_win32_rc.rc.in
CLEANFILES = vlc_win32_rc.rc

SUFFIXES = .rc.in .rc

AM_CPPFLAGS = $(CPPFLAGS_vlc)
AM_CFLAGS = $(CFLAGS_vlc)
noinst_LTLIBRARIES =
if HAVE_DBUS
AM_CPPFLAGS += -DHAVE_DBUS
endif
if HAVE_EMSCRIPTEN
AM_CFLAGS += -sMAIN_MODULE=1
endif

#
# Main Hakone executable
#
hakone_LDADD = ../lib/libvlc.la ../src/libvlccore.la
hakone_CPPFLAGS = $(AM_CPPFLAGS)
if !HAVE_WIN32
hakone_SOURCES = vlc.c override.c
hakone_LDADD += $(LIBDL)
else
hakone_SOURCES = winvlc.c
EXTRA_hakone_DEPENDENCIES = vlc_win32_rc.$(OBJEXT)
hakone_LDFLAGS = -mwindows -Wc,-static
hakone_LDADD += -lole32 vlc_win32_rc.$(OBJEXT)
libbreakpad_wrapper_la_SOURCES = breakpad.cpp
libbreakpad_wrapper_la_LIBADD = $(BREAKPAD_LIBS)
libbreakpad_wrapper_la_LDFLAGS = -static
libbreakpad_wrapper_la_CXXFLAGS = $(AM_CXXFLAGS) $(BREAKPAD_CFLAGS) -DBREAKPAD_URL=\"@BREAKPAD_URL@\"
if HAVE_BREAKPAD
noinst_LTLIBRARIES += libbreakpad_wrapper.la
hakone_LDADD += libbreakpad_wrapper.la -lstdc++ -lwininet
EXTRA_hakone_DEPENDENCIES += libbreakpad_wrapper.la
hakone_CPPFLAGS += -DHAVE_BREAKPAD
endif
endif

hakone_osx_SOURCES = darwinvlc.m
hakone_osx_CPPFLAGS = $(AM_CPPFLAGS)
hakone_osx_LDFLAGS = $(LDFLAGS_vlc) -Wl,-framework,CoreFoundation,-framework,Cocoa
hakone_osx_LDFLAGS += -Xlinker -rpath -Xlinker "$(libdir)"
if HAVE_BREAKPAD
hakone_osx_LDFLAGS += -Wl,-framework,Breakpad
hakone_osx_OBJCFLAGS = -F$(CONTRIB_DIR)/Frameworks
hakone_osx_CPPFLAGS += -DHAVE_BREAKPAD
endif
hakone_osx_LDADD = ../lib/libvlc.la

hakone_osx_static_SOURCES = $(hakone_osx_SOURCES)
hakone_osx_static_CPPFLAGS = $(hakone_osx_CPPFLAGS)
hakone_osx_static_OBJCFLAGS = $(AM_OBJCFLAGS) \
	-DTOP_BUILDDIR=\"$(abs_top_builddir)\" \
	-DTOP_SRCDIR=\"$(abs_top_srcdir)\" \
	$(NULL)
hakone_osx_static_LDFLAGS = $(hakone_osx_LDFLAGS) -static
hakone_osx_static_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../lib/.libs/"
hakone_osx_static_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../src/.libs/"
hakone_osx_static_LDFLAGS += -Xlinker -rpath -Xlinker "@executable_path/../Frameworks/"
hakone_osx_static_LDADD = $(hakone_osx_LDADD)
if HAVE_BREAKPAD
hakone_osx_static_LDFLAGS += -Wl,-framework,Breakpad
hakone_osx_static_OBJCFLAGS += -F$(CONTRIB_DIR)/Frameworks
endif


#
# Static (debug) Hakone executable
#
hakone_static_SOURCES = $(hakone_SOURCES)
hakone_static_CFLAGS = $(AM_CFLAGS) \
	-DTOP_BUILDDIR=\"$(abs_top_builddir)\" \
	-DTOP_SRCDIR=\"$(abs_top_srcdir)\" \
	$(NULL)
hakone_static_CPPFLAGS = $(hakone_CPPFLAGS)
EXTRA_hakone_static_DEPENDENCIES = $(EXTRA_hakone_DEPENDENCIES)
hakone_static_LDADD = $(hakone_LDADD)
hakone_static_LDFLAGS = $(hakone_LDFLAGS) -no-install -static

#
# Root wrapper
#
if BUILD_VLC
if !HAVE_WIN32
if !HAVE_OS2
bin_PROGRAMS += hakone-wrapper
endif
endif
endif
hakone_wrapper_SOURCES = rootwrap.c
hakone_wrapper_LDADD = $(SOCKET_LIBS)

vlc_win32_rc.rc: $(top_builddir)/config.status vlc_win32_rc.rc.in
	cd "$(top_builddir)" && \
	$(SHELL) ./config.status --file="bin/$@"

vlc_win32_rc.$(OBJEXT): vlc_win32_rc.rc $(top_builddir)/extras/package/win32/hakone.exe.manifest
	$(WINDRES) --include-dir $(top_srcdir)/share/icons --include-dir $(top_builddir)/extras/package/win32 -i $< -o $@

#
# Plug-ins cache generator
#
if HAVE_DYNAMIC_PLUGINS
pkglibexec_PROGRAMS = vlc-cache-gen
endif
vlc_cache_gen_SOURCES = cachegen.c
vlc_cache_gen_LDADD = \
	$(GNUGETOPT_LIBS) \
	../compat/libcompat.la \
	../lib/libvlc.la ../src/libvlccore.la
if HAVE_WIN32
vlc_cache_gen_LDADD += vlc_win32_rc.$(OBJEXT)
vlc_cache_gen_LDFLAGS = -Wc,-static
EXTRA_vlc_cache_gen_DEPENDENCIES = vlc_win32_rc.$(OBJEXT)
endif

if HAVE_PARTIAL_LINKING
AM_CPPFLAGS += -DVLC_PARTIAL_LINKING -I $(builddir)/../modules/
if !HAVE_WIN32
hakone_SOURCES += vlc_static_modules.c
endif
hakone_osx_SOURCES += vlc_static_modules.c
hakone_LDADD += ../modules/libvlc_static_modules.la
hakone_osx_LDADD += ../modules/libvlc_static_modules.la
endif

#
# Plug-ins cache
#
if HAVE_DYNAMIC_PLUGINS
noinst_DATA = ../modules/plugins.dat
endif
MOSTLYCLEANFILES = $(noinst_DATA)

if HAVE_OSX
if BUILD_VLC
install-data-local:
	cd "$(DESTDIR)$(bindir)"; mv hakone-osx hakone

endif
endif

.PHONY: ../modules/plugins.dat

../modules/plugins.dat: vlc-cache-gen$(EXEEXT)
	$(AM_V_at)rm -f ../modules/plugins.dat
	$(AM_V_GEN)if test "$(build)" = "$(host)"; then \
		./vlc-cache-gen$(EXEEXT) `cd "../modules" ; pwd -P` ; \
	else \
		echo "Cross-compilation: cache generation skipped!" ; \
	fi

include preparser/Makefile.am
