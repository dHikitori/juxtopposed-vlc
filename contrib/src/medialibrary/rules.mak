MEDIALIBRARY_VERSION := 0.13.2
MEDIALIBRARY_URL := $(VIDEOLAN_GIT)/videolan/medialibrary/-/archive/$(MEDIALIBRARY_VERSION)/medialibrary-$(MEDIALIBRARY_VERSION).tar.bz2

ifndef HAVE_WINSTORE
PKGS += medialibrary
endif
ifeq ($(call need_pkg,"medialibrary >= 0.13.0"),)
PKGS_FOUND += medialibrary
endif

DEPS_medialibrary = sqlite $(DEPS_sqlite)

$(TARBALLS)/medialibrary-$(MEDIALIBRARY_VERSION).tar.bz2:
	$(call download_pkg,$(MEDIALIBRARY_URL),medialibrary)

.sum-medialibrary: medialibrary-$(MEDIALIBRARY_VERSION).tar.bz2

medialibrary: medialibrary-$(MEDIALIBRARY_VERSION).tar.bz2 .sum-medialibrary
	$(UNPACK)
	$(APPLY) $(SRC)/medialibrary/0001-preserve-windows-unc-storage-paths.patch
	$(APPLY) $(SRC)/medialibrary/0002-exclude-associated-tracks-from-media-lookup.patch
	$(APPLY) $(SRC)/medialibrary/0003-wait-for-transient-sqlite-locks.patch
	$(APPLY) $(SRC)/medialibrary/0004-replace-external-track-assignments.patch
	$(APPLY) $(SRC)/medialibrary/0005-persist-discovered-track-selections.patch
	$(APPLY) $(SRC)/medialibrary/0006-wake-eligible-priority-writers.patch
	$(APPLY) $(SRC)/medialibrary/0007-atomic-addon-playlist-append.patch
	$(MOVE)

.medialibrary: medialibrary crossfile.meson
	$(MESONCLEAN)
	$(MESON) -Dlibvlc=disabled -Dlibtool_workaround=true
	+$(MESONBUILD)
	touch $@
