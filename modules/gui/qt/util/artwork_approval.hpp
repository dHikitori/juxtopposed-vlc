// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "artwork_selection.hpp"

// A selected file needs a private ArtworkSelection::Receipt. A raw file URI
// does not prove that a user selected and prepared its pixels. Native callers
// use stampCopy for the tag writer, then apply for the live item CAS.
