// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addon_credentials.hpp"
#include "addon_package_store.hpp"

namespace vlc::addons {
// Prepare this host data on the Manager thread. Invoke its readers only on
// joined Qt work. No reader owns or accesses the Manager.
//
// credentialPresence is a seed from memory. Use credentialReader on joined work
// when a declared reference has no cached presence. Probe workerExecutable there;
// this factory does not test the file or initialize a network backend.
//
// Discard this source and its credential reader before the native Plan keeper
// is retained. Do not expose them to a guest or to QML.
struct MetadataCatalogueSource
{
    MetadataCatalogueReader catalogueReader;
    CredentialStore::Reader credentialReader;
    QHash<QString, QHash<QString, bool>> credentialPresence;
    QString workerExecutable;
    StoreAuthorityToken queuedAuthority;
    bool current() const noexcept { return queuedAuthority.current(); }
};
}
