// SPDX-License-Identifier: GPL-2.0-or-later
#include "native_session_fixture.h"
// Isolate native instance/plugin startup from the Qt session adapter.
int main(void) {
    for(unsigned i=0;i<8;++i) {
        struct native_session_fixture* value=native_session_create();
        if(!value) return 1;
        native_session_destroy(value);
    }
    return 0;
}
