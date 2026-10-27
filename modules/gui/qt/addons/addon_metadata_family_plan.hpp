// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QStringList>
#include <QVariantList>

// This host table binds retained native families to their portable extraction.
// An add-on manifest cannot add a family binding or claim native authority.
class AddonMetadataFamilyPlan final
{
public:
    struct Binding { QString family, nativeContribution, portableContribution; };
    static const QList<Binding>& bindings();
    static QVariantList select(const QString& feature, const QString& operation,
        const QVariantList& eligible, const QStringList& explicitOrder, bool nativeScope);
};
