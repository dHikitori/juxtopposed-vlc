// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2021-2026 VLC authors and VideoLAN
#include "firstrunwizard.hpp"
#include "dialogs/preferences/redesign_preferences.hpp"
#include "dialogs/toolbar/controlbar_profile_model.hpp"
#include "medialibrary/medialib.hpp"
#include "util/color_scheme_model.hpp"

#include <QColor>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <vlc_configuration.h>
#include <vlc_cxx_helpers.hpp>

FirstRunWizard::FirstRunWizard(QObject* parent) : QObject(parent) {}

void FirstRunWizard::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    m_ctx = ctx;
    emit ctxChanged();
}

bool FirstRunWizard::hasMediaLibrary() const
{
    return m_ctx && vlc_ml_instance_get(m_ctx->getIntf());
}

QString FirstRunWizard::defaultFolder() const
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (path.isEmpty() || !QFileInfo(path).isDir()) return {};
    const auto home = QDir::homePath();
    return QDir::toNativeSeparators(path.startsWith(home + QLatin1Char('/')) ? QStringLiteral("~") + path.mid(home.size()) : path);
}

QString FirstRunWizard::folderPath(const QString& folder)
{
    QString path = QDir::fromNativeSeparators(folder.trimmed());
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
        path.replace(0, 1, QDir::homePath());
    return path.isEmpty() ? QString() : QDir(path).absolutePath();
}

bool FirstRunWizard::fail(const QString& error)
{
    m_error = error;
    emit errorChanged();
    return false;
}

bool FirstRunWizard::validateFolder(const QString& folder)
{
    if (hasMediaLibrary() && !folder.trimmed().isEmpty()) {
        const QFileInfo selected(folderPath(folder));
        if (!selected.isDir() || !selected.isReadable())
            return fail(qtr("Select an existing folder, or clear the field to skip this step."));
    }
    m_error.clear(); emit errorChanged(); return true;
}

QString FirstRunWizard::browseFolder(const QString& initial)
{
    return QFileDialog::getExistingDirectory(nullptr, qtr("Select a media folder"), folderPath(initial));
}

QVariantList FirstRunWizard::shortcuts() const
{
    if (!m_ctx) return {};
    RedesignPreferences preferences;
    preferences.setCtx(m_ctx);
    const auto key = [&](const QString& name) {
        const auto first = preferences.value(name).toString().section(QLatin1Char('\t'), 0, 0);
        return first.isEmpty() ? qtr("Not set") : QKeySequence::fromString(first, QKeySequence::PortableText).toString(QKeySequence::NativeText);
    };
    QVariantList rows;
    const auto row = [&](const QString& title, const QStringList& keys) {
        rows.append(QVariantMap{{QStringLiteral("title"), title}, {QStringLiteral("keys"), keys}});
    };
    row(qtr("Play / Pause"), {key(QStringLiteral("key-play-pause"))});
    row(qtr("Seek Backward / Forward"), {key(QStringLiteral("key-jump-short")), key(QStringLiteral("key-jump+short"))});
    row(qtr("Previous / Next Item"), {key(QStringLiteral("key-prev")), key(QStringLiteral("key-next"))});
    row(qtr("Previous / Next Frame"), {key(QStringLiteral("key-frame-previous")), key(QStringLiteral("key-frame-next"))});
    row(qtr("Cycle Subtitle Track"), {key(QStringLiteral("key-subtitle-track"))});
    row(qtr("Cycle Audio Track"), {key(QStringLiteral("key-audio-track"))});
    row(qtr("Toggle Fullscreen"), {key(QStringLiteral("key-toggle-fullscreen"))});
    row(qtr("Increase / Decrease Volume"), {key(QStringLiteral("key-vol-up")), key(QStringLiteral("key-vol-down"))});
    // The queue shortcut is declared by GlobalShortcuts.qml, not a core action.
    row(qtr("Show / Hide Queue"), {QKeySequence(QStringLiteral("Ctrl+L")).toString(QKeySequence::NativeText)});
    row(qtr("Show / Hide File Browser"), {key(QStringLiteral("key-toggle-library"))});
    row(qtr("Take a Snapshot"), {key(QStringLiteral("key-snapshot"))});
    row(qtr("Record"), {key(QStringLiteral("key-record"))});
    return rows;
}

bool FirstRunWizard::finish(const QString& folder, int scheme, const QString& accent, bool onlineMetadata)
{
    if (!m_ctx || !validateFolder(folder)) return false;
    const QColor color(accent);
    if (scheme < ColorSchemeModel::System || scheme > ColorSchemeModel::Night || !color.isValid())
        return fail(qtr("Select a theme and color."));

    // Check the interface file with a fresh handle: an earlier failed sync
    // must not prevent a later retry from succeeding.
    const auto* stored = m_ctx->getIntf()->mainSettings;
    const auto schemeKey = QStringLiteral("MainWindow/color-scheme-index");
    QSettings settings(stored->fileName(), stored->format());
    const auto oldScheme = settings.value(schemeKey);
    settings.setValue(schemeKey, scheme);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        // Restore the shared settings cache even when the file is unavailable.
        if (oldScheme.isValid()) settings.setValue(schemeKey, oldScheme);
        else settings.remove(schemeKey);
        settings.sync();
        return fail(qtr("Hakone cannot save the theme. Make sure that you have access to the settings folder. Try again."));
    }

    RedesignPreferences preferences;
    preferences.setCtx(m_ctx);
    preferences.stage(QStringLiteral("metadata-network-access"), onlineMetadata);
    preferences.stage(QStringLiteral("qt-privacy-ask"), false);
    preferences.stage(QStringLiteral("qt-accent-color"), color.name(QColor::HexRgb));
    preferences.stage(QStringLiteral("qt-menubar"), false);
    preferences.stage(QStringLiteral("qt-titlebar"), false);
    preferences.stage(QStringLiteral("qt-pin-controls"), false);
    if (!preferences.save()) {
        if (oldScheme.isValid()) settings.setValue(schemeKey, oldScheme);
        else settings.remove(schemeKey);
        settings.sync();
        return fail(preferences.errorMessage());
    }
    m_ctx->getColorScheme()->setCurrentIndex(scheme);

    auto* profiles = m_ctx->controlbarProfileModel();
    std::unique_ptr<ControlbarProfile> profile(profiles->generateProfileFromStyle(ControlbarProfileModel::Style::DEFAULT_STYLE));
    const auto existing = profiles->findModel(profile.get());
    if (existing) profiles->setSelectedProfile(*existing);
    else profiles->insertProfile(std::move(profile), true);
    profiles->save();

    // Scanning runs on the media-library thread, as in the native folder editor.
    if (hasMediaLibrary() && !folder.trimmed().isEmpty()) {
        const auto url = QUrl::fromLocalFile(folderPath(folder)).toEncoded();
        m_ctx->getMediaLibrary()->runOnMLThread(this, [url](vlc_medialibrary_t* library) {
            vlc_ml_add_folder(library, url.constData());
        });
    }
    if (auto* library = m_ctx->getMediaLibrary()) library->reload();
    m_error.clear(); emit errorChanged(); return true;
}

bool FirstRunWizard::skip()
{
    if (!m_ctx) return false;
    RedesignPreferences preferences;
    preferences.setCtx(m_ctx);
    // Closing setup keeps the existing network choice and theme unchanged.
    preferences.stage(QStringLiteral("qt-privacy-ask"), false);
    if (!preferences.save()) return fail(preferences.errorMessage());
    if (auto* library = m_ctx->getMediaLibrary()) library->reload();
    m_error.clear(); emit errorChanged(); return true;
}
