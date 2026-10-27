// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
void addon_test_native_provider_queued(bool);
void addon_test_native_provider_complete(void);
unsigned addon_test_native_provider_pending(void);
void addon_test_native_provider_block_delete(bool);
unsigned addon_test_native_provider_deleted(void);
#ifdef __cplusplus
}
#endif
