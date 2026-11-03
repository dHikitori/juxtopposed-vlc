// SPDX-License-Identifier: GPL-2.0-or-later
#include "redesign_preferences.hpp"
#include "dialogs/dialogs/dialogmodel.hpp"
#include "player/stereo_test_player.hpp"
#include "player/player_controller.hpp"
#include "util/renderer_manager.hpp"
#include "util/file_log_preferences.hpp"
#include "util/preference_values.hpp"
#include "util/vlcqtmessagehandler.hpp"
#include <vlc_configuration.h>
#include <vlc_modules.h>
#include <vlc_interface.h>
#include <vlc_plugin.h>
#include <vlc_aout.h>
#include <vlc_player.h>
#include <vlc_playlist.h>
#include <vlc_iso_lang.h>
#include <vlc_subpicture.h>
#include <vlc_vout.h>
#include <vlc_vout_osd.h>
#include "../../../../audio_filter/equalizer_presets.h"
#include <cmath>
#include <climits>
#include <algorithm>
#include <utility>
#include <QFileDialog>
#include <QColorDialog>
#include <QGuiApplication>
#include <QWindow>
#include <QScreen>
#include <QKeyEvent>
#include <QRegularExpression>
#include <QCollator>
#include <QLocale>
#include <QMap>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QSettings>
#include <QSignalBlocker>
#include <vlc_actions.h>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDataStream>
#include <QCryptographicHash>
#include <QUuid>
#include "util/vlchotkeyconverter.hpp"
#include "util/postprocessing_preferences.hpp"
#include "util/interface_translator.hpp"
#include "medialibrary/medialib.hpp"
#include "../../../../misc/medialibrary/history_filter.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
const QString presetOption = QStringLiteral("qt-player-presets");
const QString applicationProfileOption = QStringLiteral("qt-application-profiles");
constexpr qsizetype applicationProfileLimit = 16 * 1024 * 1024;

QStringList applicationOptions()
{
    QStringList names;
    config_ForEachPersistent([](void* opaque, const char* name) {
        const auto option = qfu(name);
        if (option != applicationProfileOption)
            static_cast<QStringList*>(opaque)->append(option);
    }, &names);
    names.removeDuplicates();
    std::sort(names.begin(), names.end());
    return names;
}

QVariantMap encodeInterface(const QVariantMap& values)
{
    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_0);
    stream << values;
    return {{QStringLiteral("data"), QString::fromLatin1(data.toBase64())},
        {QStringLiteral("checksum"), QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())}};
}

bool decodeInterface(const QVariant& encoded, QVariantMap& result)
{
    if (encoded.metaType().id() != QMetaType::QVariantMap) return false;
    const auto map = encoded.toMap();
    const auto text = map.value(QStringLiteral("data")).toString().toLatin1();
    if (text.size() > 2 * 1024 * 1024) return false;
    const auto decoded = QByteArray::fromBase64Encoding(text, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || decoded.decoded.size() > 1024 * 1024
        || QCryptographicHash::hash(decoded.decoded, QCryptographicHash::Sha256).toHex()
            != map.value(QStringLiteral("checksum")).toString().toLatin1()) return false;
    QDataStream stream(decoded.decoded);
    stream.setVersion(QDataStream::Qt_6_0);
    stream >> result;
    return stream.status() == QDataStream::Ok && stream.atEnd() && result.size() <= 8192;
}
// Minimum pointer movement in pixels, from low to high sensitivity.
constexpr int fullscreenSensitivityLevels[] = {32, 16, 8, 3, 2, 1, 0};

void applyPlaybackPreferences(qt_intf_t* intf, const QVariantMap* changes = nullptr)
{
    const auto changed = [changes](const char* name) {
        return !changes || changes->contains(QLatin1String(name));
    };
    if (changed("playlist-autostart")) {
        auto* instance = vlc_object_instance(intf);
        if (var_Type(instance, "playlist-autostart") == 0)
            var_Create(instance, "playlist-autostart", VLC_VAR_BOOL);
        var_SetBool(instance, "playlist-autostart", bool(config_GetInt("playlist-autostart")));
    }
    // The player and playlist cache these values at creation. Changing their
    // saved defaults alone leaves the active queue using the old behavior.
    auto* playlist = intf->p_playlist;
    vlc_playlist_Lock(playlist);
    auto* player = vlc_playlist_GetPlayer(playlist);
    if (changed("random"))
        vlc_playlist_SetPlaybackOrder(playlist, config_GetInt("random")
            ? VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM : VLC_PLAYLIST_PLAYBACK_ORDER_NORMAL);
    if (changed("repeat") || changed("loop"))
        vlc_playlist_SetPlaybackRepeat(playlist, config_GetInt("repeat")
            ? VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT : config_GetInt("loop")
            ? VLC_PLAYLIST_PLAYBACK_REPEAT_ALL : VLC_PLAYLIST_PLAYBACK_REPEAT_NONE);
    if (changed("play-and-exit") || changed("play-and-stop"))
        vlc_playlist_SetMediaStoppedAction(playlist, config_GetInt("play-and-exit")
            ? VLC_PLAYLIST_MEDIA_STOPPED_EXIT : config_GetInt("play-and-stop")
            ? VLC_PLAYLIST_MEDIA_STOPPED_STOP : VLC_PLAYLIST_MEDIA_STOPPED_CONTINUE);
    if (changed("play-and-pause"))
        vlc_player_SetPlayAndPause(player, bool(config_GetInt("play-and-pause")));
    if (changed("playlist-cork"))
        vlc_player_SetPauseOnCork(player, bool(config_GetInt("playlist-cork")));
    if (changed("start-paused"))
        vlc_player_SetStartPaused(player, bool(config_GetInt("start-paused")));
    if (changed("input-repeat"))
        vlc_player_SetRepeatCount(player, config_GetInt("input-repeat"));
    if (changed("rate"))
        vlc_player_ChangeRate(player, config_GetFloat("rate"));
    vlc_playlist_Unlock(playlist);
}

QString visualizationValue(qt_intf_t* intf, const char* name)
{
    auto* output = vlc_player_aout_Hold(intf->p_player);
    char* text = var_InheritString(output ? VLC_OBJECT(output) : VLC_OBJECT(intf), name);
    const QString result = qfu(text ? text : "");
    free(text);
    if (output) aout_Release(output);
    return result;
}

void applyVisualizationPreferences(qt_intf_t* intf, bool moduleChanged, bool effectsChanged)
{
    auto* instance = vlc_object_instance(intf);
    for (const auto* name : {"audio-visual", "effect-list"}) {
        if (!(moduleChanged && !strcmp(name, "audio-visual"))
            && !(effectsChanged && !strcmp(name, "effect-list"))) continue;
        if (!config_FindConfig(name)) continue;
        char* text = config_GetPsz(name);
        if (var_Type(instance, name) == 0) var_Create(instance, name, VLC_VAR_STRING);
        var_SetString(instance, name, text ? text : "");
        free(text);
    }
    auto* output = vlc_player_aout_Hold(intf->p_player);
    if (!output) return;
    if (effectsChanged && config_FindConfig("effect-list")) {
        char* effects = config_GetPsz("effect-list");
        if (var_Type(output, "effect-list") == 0) var_Create(output, "effect-list", VLC_VAR_STRING);
        var_SetString(output, "effect-list", effects ? effects : "");
        free(effects);
    }
    char* module = moduleChanged ? config_GetPsz("audio-visual") : var_GetString(output, "audio-visual");
    const QString selected = qfu(module ? module : "none");
    free(module);
    if (moduleChanged || selected == QLatin1String("visual")) {
        QString choice = selected == QLatin1String("none") ? QString() : selected;
        if (selected == QLatin1String("visual")) {
            char* effects = var_InheritString(output, "effect-list");
            const QString effect = qfu(effects ? effects : "");
            free(effects);
            // Single effects share the native menu values. A custom effect
            // list still uses the visual module and keeps the complete list.
            for (const auto* name : {"dummy", "scope", "spectrometer", "spectrum", "vuMeter"})
                if (effect.compare(QLatin1String(name), Qt::CaseInsensitive) == 0) {
                    choice = qfu(name);
                    break;
                }
        }
        var_SetString(output, "visual", choice.toUtf8().constData());
    }
    aout_Release(output);
}

QVariantList moduleChoices(const QStringList& capabilities)
{
    QMap<QString, QStringList> labels;
    size_t count = 0;
    auto** modules = module_list_get(&count);
    for (size_t i = 0; i < count; ++i) {
        auto* module = modules[i];
        if (!capabilities.contains(qfu(module_get_capability(module)))) continue;
        const auto id = qfu(module_get_object(module));
        if (id == QLatin1String("unnamed") || id == QLatin1String("any") || id == QLatin1String("none")) continue;
        auto& names = labels[id];
        const auto label = qtr(module_GetLongName(module));
        if (!names.contains(label)) names.append(label);
    }
    module_list_free(modules);
    QVariantList choices;
    for (auto it = labels.cbegin(); it != labels.cend(); ++it)
        choices.append(QVariantMap{{QStringLiteral("value"), it.key()},
            {QStringLiteral("label"), QStringLiteral("%1 (%2)").arg(it.value().join(QStringLiteral(" / ")), it.key())}});
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(choices.begin(), choices.end(), [&](const auto& a, const auto& b) {
        return collator.compare(a.toMap().value(QStringLiteral("label")).toString(),
            b.toMap().value(QStringLiteral("label")).toString()) < 0;
    });
    return choices;
}

QString configChoiceText(const module_config_t* cfg, ssize_t index,
                         const char* value, const char* text)
{
    // The core choice APIs translate labels with the startup locale. Use the
    // canonical source for UI-owned labels so live changes do not keep it.
    if (cfg->i_type == CONFIG_ITEM_MODULE && value) {
        if (qstrcmp(value, "any") == 0) return qtr("Automatic");
        if (qstrcmp(value, "none") == 0) return qtr("Disable");
        if (auto* module = module_find(value)) return qtr(module_GetLongName(module));
    } else if (index >= 0 && index < cfg->list_count && cfg->list_text) {
        return cfg->list_text[index] ? qtr(cfg->list_text[index]) : QString();
    }
    // Dynamic choices can contain device names and other user data.
    return qfu(text ? text : "");
}

const QVariantList& installedInterfaceLanguages()
{
    // Catalogs are installed outside the running app. Refresh on the next
    // launch, rather than probing every file again on each preferences edit.
    static const QVariantList installed = [] {
        QVariantList rows{{QVariantMap{{QStringLiteral("label"), QStringLiteral("English")},
                                      {QStringLiteral("value"), QStringLiteral("en")}}}};
#ifdef ENABLE_NLS
        char *path = config_GetSysPath(VLC_LOCALE_DIR, nullptr);
        const QDir directory(qfu(path ? path : ""));
        const bool havePath = path != nullptr;
        free(path);
        for (const auto& code : havePath ? directory.entryList(QDir::Dirs | QDir::NoDotAndDotDot) : QStringList{}) {
            if (code == QLatin1String("en")) continue;
            const auto catalog = directory.filePath(code + QStringLiteral("/LC_MESSAGES/")
                + QStringLiteral(PACKAGE_NAME) + QStringLiteral(".mo"));
            const QFileInfo file(catalog);
            if (!file.isFile() || !file.isReadable()) continue;
            const QLocale locale(code);
            QString label;
            if (locale.language() != QLocale::C) {
                label = locale.nativeLanguageName();
                if (code.contains(QLatin1Char('_')) || code.contains(QLatin1Char('-')))
                    label += QStringLiteral(" (%1)").arg(locale.nativeTerritoryName());
            } else {
                const auto* language = vlc_find_iso639(code.toUtf8().constData(), true);
                label = language ? qfu(language->psz_eng_name) : code;
            }
            if (!label.isEmpty()) label[0] = label[0].toUpper();
            rows.append(QVariantMap{{QStringLiteral("label"), label}, {QStringLiteral("value"), code}});
        }
#endif
        QCollator collator;
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(rows.begin(), rows.end(), [&](const auto& a, const auto& b) {
            return collator.compare(a.toMap().value(QStringLiteral("label")).toString(),
                b.toMap().value(QStringLiteral("label")).toString()) < 0;
        });
        return rows;
    }();
    return installed;
}

bool installedInterfaceLanguage(const QString& code)
{
    return code == QLatin1String("auto") || std::any_of(installedInterfaceLanguages().cbegin(),
        installedInterfaceLanguages().cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == code;
        });
}

vlc_playlist_preparsing directoryDepth(const QString& value)
{
    if (value.compare(QLatin1String("none"), Qt::CaseInsensitive) == 0)
        return VLC_PLAYLIST_PREPARSING_ENABLED;
    if (value.compare(QLatin1String("expand"), Qt::CaseInsensitive) == 0)
        return VLC_PLAYLIST_PREPARSING_RECURSIVE;
    return VLC_PLAYLIST_PREPARSING_COLLAPSE;
}

vlc_playlist_preparsing directoryDepth(MainCtx* ctx)
{
    auto* playlist = ctx->getIntf()->p_playlist;
    vlc_playlist_Lock(playlist);
    const auto depth = vlc_playlist_GetPreparseDepth(playlist);
    vlc_playlist_Unlock(playlist);
    return depth;
}

const char *const bufferingOptions[] = {"file-caching", "disc-caching", "network-caching", "live-caching"};
// Retain VLC's caching levels while using each input type's current defaults.
// In particular, Normal must not reduce the default local file buffer.
const int bufferingLevels[] = {300, 100, 200, 500, 1000};

qint64 bufferingValue(const char *name, int level)
{
    const auto *cfg = config_FindConfig(name);
    return std::clamp<qint64>(cfg->orig.i * level / 300, cfg->min.i, cfg->max.i);
}

bool isBufferingOption(const QString& name)
{
    return std::any_of(std::begin(bufferingOptions), std::end(bufferingOptions),
        [&](const char *option) { return name == QLatin1String(option); });
}

struct ResamplingQuality {
    const char* module;
    const char* option;
    int low, balanced, high;
};

const ResamplingQuality resamplingQualities[] = {
    {"soxr", "soxr-resampler-quality", 0, 2, 4},
    {"samplerate", "src-converter-type", 4, 2, 0},
    {"speex_resampler", "speex-resampler-quality", 0, 4, 10},
};

const ResamplingQuality* resamplingQuality(const QString& module)
{
    const bool automatic = module.isEmpty() || module == QLatin1String("any");
    for (const auto& quality : resamplingQualities) {
        if ((automatic || module == QLatin1String(quality.module)
             || (module == QLatin1String("speex") && !strcmp(quality.module, "speex_resampler")))
            && config_FindConfig(quality.option)) return &quality;
    }
    return nullptr;
}

QString audioPassthroughOption(const QString& module)
{
    const auto option = module == QLatin1String("mmdevice") ? QStringLiteral("mmdevice-passthrough")
        : module == QLatin1String("alsa") ? QStringLiteral("alsa-passthrough")
        : module == QLatin1String("oss") ? QStringLiteral("oss-spdif")
        : module == QLatin1String("waveout") ? QStringLiteral("spdif") : QString();
    const auto* cfg = option.isEmpty() ? nullptr : config_FindConfig(option.toUtf8().constData());
    return cfg && cfg->psz_text ? option : QString();
}

QString audioFilterHelp(const QString& name)
{
    if (name == QLatin1String("equalizer")) return qtr("Change the level of each frequency band.");
    if (name == QLatin1String("normvol")) return qtr("Reduce the volume when the average audio level is above the target level.");
    if (name == QLatin1String("compressor")) return qtr("Reduce the difference between loud and quiet sounds.");
    if (name == QLatin1String("stereo_widen")) return qtr("Increase the separation between the left and right audio channels.");
    if (name == QLatin1String("chorus_flanger")) return qtr("Mix the audio with delayed copies to change its sound.");
    if (name == QLatin1String("karaoke")) return qtr("Reduce sounds in the center of stereo audio.");
    if (name == QLatin1String("spatializer")) return qtr("Add the sound of a room to stereo audio.");
    if (name == QLatin1String("audioscrobbler")) return qtr("Send information about played tracks to Last.fm.");
    return {};
}

const QStringList& videoFilterOptions()
{
    static const QStringList names{
        QStringLiteral("postproc-q"),
        QStringLiteral("sharpen-sigma"), QStringLiteral("gradfun-radius"), QStringLiteral("grain-variance"),
        QStringLiteral("extract-component"), QStringLiteral("colorthres-color"),
        QStringLiteral("colorthres-saturationthres"), QStringLiteral("colorthres-similaritythres"),
        QStringLiteral("posterize-level"), QStringLiteral("gradient-mode"), QStringLiteral("gradient-color"),
        QStringLiteral("gradient-cartoon"), QStringLiteral("sepia-intensity"),
        QStringLiteral("brightness-threshold"), QStringLiteral("puzzle-rows"), QStringLiteral("puzzle-cols"),
        QStringLiteral("puzzle-mode"), QStringLiteral("puzzle-rotation"), QStringLiteral("puzzle-border"),
        QStringLiteral("puzzle-preview"), QStringLiteral("puzzle-preview-size"), QStringLiteral("puzzle-shape-size"),
        QStringLiteral("puzzle-auto-shuffle"), QStringLiteral("puzzle-auto-solve"),
        QStringLiteral("erase-mask"), QStringLiteral("erase-x"), QStringLiteral("erase-y"),
        QStringLiteral("blur-factor"), QStringLiteral("gaussianblur-sigma"),
        QStringLiteral("antiflicker-window-size"), QStringLiteral("antiflicker-softening-size"),
        QStringLiteral("hqdn3d-luma-spat"), QStringLiteral("hqdn3d-chroma-spat"),
        QStringLiteral("hqdn3d-luma-temp"), QStringLiteral("hqdn3d-chroma-temp"), QStringLiteral("anaglyph-scheme")
    };
    return names;
}

const QStringList& audioFilterFloatOptions()
{
    static const QStringList names{
        QStringLiteral("spatializer-roomsize"), QStringLiteral("spatializer-width"),
        QStringLiteral("spatializer-wet"), QStringLiteral("spatializer-dry"), QStringLiteral("spatializer-damp"),
        QStringLiteral("pitch-shift")
    };
    return names;
}

const QStringList& overlayOptions()
{
    static const QStringList names{
        QStringLiteral("marq-marquee"), QStringLiteral("marq-file"),
        QStringLiteral("marq-x"), QStringLiteral("marq-y"), QStringLiteral("marq-position"),
        QStringLiteral("marq-color"), QStringLiteral("marq-size"), QStringLiteral("marq-opacity"),
        QStringLiteral("marq-timeout"), QStringLiteral("marq-refresh"),
        QStringLiteral("logo-file"), QStringLiteral("logo-x"), QStringLiteral("logo-y"),
        QStringLiteral("logo-position"), QStringLiteral("logo-opacity"),
        QStringLiteral("logo-delay"), QStringLiteral("logo-repeat")
    };
    return names;
}

const QStringList& presetOptions()
{
    // Media presentation and player layout belong in a show preset. Overlay images and
    // text files are presentation assets. Do not capture device selections,
    // credentials, general file paths or other machine preferences.
    static const QStringList names = [] {
        QStringList names{
            QStringLiteral("qt-subtitle-timeline"), QStringLiteral("qt-audio-waveform"),
            QStringLiteral("audio-visual"), QStringLiteral("effect-list"),
            QStringLiteral("qt-player-layout"),
            QStringLiteral("audio-track"), QStringLiteral("audio-track-id"), QStringLiteral("audio-language"),
            QStringLiteral("sub-track"), QStringLiteral("sub-track-id"), QStringLiteral("sub-language"),
            QStringLiteral("audio-desync"), QStringLiteral("sub-delay"), QStringLiteral("sub-delay-ms"), QStringLiteral("sub-fps"),
            QStringLiteral("stereo-mode"), QStringLiteral("mix-mode"), QStringLiteral("audio-spatial"), QStringLiteral("audio-spatial-mode"),
            QStringLiteral("audio-filter"), QStringLiteral("equalizer-preset"), QStringLiteral("equalizer-bands"),
            QStringLiteral("equalizer-preamp"), QStringLiteral("equalizer-2pass"), QStringLiteral("norm-max-level"),
            QStringLiteral("compressor-rms-peak"), QStringLiteral("compressor-attack"), QStringLiteral("compressor-release"),
            QStringLiteral("compressor-threshold"), QStringLiteral("compressor-ratio"), QStringLiteral("compressor-knee"), QStringLiteral("compressor-makeup-gain"),
            QStringLiteral("video-filter"), QStringLiteral("hue"), QStringLiteral("brightness"), QStringLiteral("contrast"),
            QStringLiteral("saturation"), QStringLiteral("gamma"), QStringLiteral("transform-type"), QStringLiteral("rotate-angle"), QStringLiteral("qt-video-transform-state"),
            QStringLiteral("fit"), QStringLiteral("autoscale"), QStringLiteral("zoom"), QStringLiteral("aspect-ratio"), QStringLiteral("crop"), QStringLiteral("deinterlace"), QStringLiteral("deinterlace-mode"),
            QStringLiteral("subsdec-formatted"), QStringLiteral("sub-margin"), QStringLiteral("sub-text-scale"),
            QStringLiteral("sub-text-use-embedded-style"), QStringLiteral("sub-text-size-mode"), QStringLiteral("sub-text-size-pixels"),
            QStringLiteral("sub-text-vertical-position"),
            QStringLiteral("subsdelay-factor"), QStringLiteral("subsdelay-mode"), QStringLiteral("sub-filter"),
            QStringLiteral("freetype-font"), QStringLiteral("freetype-fontsize"), QStringLiteral("freetype-rel-fontsize"),
            QStringLiteral("freetype-color"), QStringLiteral("freetype-opacity"), QStringLiteral("freetype-bold"),
            QStringLiteral("freetype-outline-thickness"), QStringLiteral("freetype-outline-color"), QStringLiteral("freetype-outline-opacity"),
            QStringLiteral("freetype-shadow-angle"), QStringLiteral("freetype-shadow-distance"), QStringLiteral("freetype-shadow-color"),
            QStringLiteral("freetype-shadow-opacity"), QStringLiteral("freetype-background-color"), QStringLiteral("freetype-background-opacity"),
            QStringLiteral("freetype-background-mode")
        };
        names += videoFilterOptions();
        names += overlayOptions();
        names += audioFilterFloatOptions();
        names.append(QStringLiteral("sub-source"));
        names += {QStringLiteral("preferred-resolution"), QStringLiteral("d3d11-hdr-mode")};
        return names;
    }();
    return names;
}

bool subtitleStyleOption(const QString& name)
{
    return name.startsWith(QLatin1String("freetype-")) || name.startsWith(QLatin1String("sub-text-"));
}

void applyWindowOpacity(MainCtx* ctx)
{
    auto* intf = ctx->getIntf();
    const float opacity = config_GetFloat("qt-opacity");
    if (var_Type(intf, "qt-opacity") == 0) var_Create(intf, "qt-opacity", VLC_VAR_FLOAT);
    var_SetFloat(intf, "qt-opacity", opacity);
    if (auto* window = ctx->intfMainWindow()) window->setOpacity(opacity);
}

void applyFullscreenControlPreferences(qt_intf_t* intf, bool opacity, bool sensitivity)
{
    if (opacity) {
        if (var_Type(intf, "qt-fs-opacity") == 0) var_Create(intf, "qt-fs-opacity", VLC_VAR_FLOAT);
        var_SetFloat(intf, "qt-fs-opacity", config_GetFloat("qt-fs-opacity"));
    }
    if (sensitivity) {
        if (var_Type(intf, "qt-fs-sensitivity") == 0) var_Create(intf, "qt-fs-sensitivity", VLC_VAR_INTEGER);
        var_SetInteger(intf, "qt-fs-sensitivity", config_GetInt("qt-fs-sensitivity"));
    }
}

void applyOsdPreferences(qt_intf_t* intf, const QVariantMap& changed)
{
    auto* instance = vlc_object_instance(intf);
    const auto apply = [&changed](auto* object) {
        for (const auto* name : {"osd", "video-title-show"}) {
            if (!changed.contains(QLatin1String(name))) continue;
            if (var_Type(object, name) == 0) var_Create(object, name, VLC_VAR_BOOL);
            var_SetBool(object, name, bool(config_GetInt(name)));
        }
        for (const auto* name : {"osd-duration", "video-title-timeout", "video-title-position"}) {
            if (!changed.contains(QLatin1String(name))) continue;
            if (var_Type(object, name) == 0) var_Create(object, name, VLC_VAR_INTEGER);
            var_SetInteger(object, name, config_GetInt(name));
        }
    };
    apply(instance);

    size_t count = 0;
    vlc_player_Lock(intf->p_player);
    auto** outputs = vlc_player_vout_HoldAll(intf->p_player, &count);
    vlc_player_Unlock(intf->p_player);
    for (size_t i = 0; i < count; ++i) {
        apply(outputs[i]);
        if (changed.contains(QStringLiteral("osd")) && !config_GetInt("osd")) {
            // Stop current feedback as well as future messages. The filename
            // has its own channel and keeps its independent display setting.
            for (int channel : {VOUT_SPU_CHANNEL_OSD, VOUT_SPU_CHANNEL_OSD_HSLIDER,
                                VOUT_SPU_CHANNEL_OSD_VSLIDER})
                vout_FlushSubpictureChannel(outputs[i], channel);
        }
        vout_Release(outputs[i]);
    }
    free(outputs);
}

const QStringList& videoOutputOptions()
{
    // Output variables own the current presentation. Hotkeys and the native
    // choice models can change these without changing saved preferences.
    // Apply fit before autoscale: disabling automatic scaling must win over
    // a retained fit mode. Set the chain last so new filters inherit values.
    static const QStringList names = [] {
        QStringList names{
            QStringLiteral("fit"), QStringLiteral("autoscale"), QStringLiteral("zoom"), QStringLiteral("aspect-ratio"), QStringLiteral("crop"),
            QStringLiteral("deinterlace-mode"), QStringLiteral("deinterlace"),
            QStringLiteral("hue"), QStringLiteral("brightness"), QStringLiteral("contrast"),
            QStringLiteral("saturation"), QStringLiteral("gamma"),
            QStringLiteral("transform-type"), QStringLiteral("rotate-angle")
        };
        names += videoFilterOptions();
        names += overlayOptions();
        names.append(QStringLiteral("video-filter"));
        names.append(QStringLiteral("sub-source"));
        return names;
    }();
    return names;
}

QVariant outputValue(vout_thread_t* output, const QString& name)
{
    const auto bytes = name.toUtf8();
    switch (var_Type(output, bytes.constData()) & VLC_VAR_CLASS) {
    case VLC_VAR_BOOL: return var_GetBool(output, bytes.constData());
    case VLC_VAR_INTEGER: return QVariant::fromValue<qlonglong>(var_GetInteger(output, bytes.constData()));
    case VLC_VAR_FLOAT: return var_GetFloat(output, bytes.constData());
    case VLC_VAR_STRING: {
        char* text = var_GetString(output, bytes.constData());
        const auto result = qfu(text ? text : "");
        free(text);
        return result;
    }
    default: return {};
    }
}

bool validPresetName(const QString& name, const QVariantList& presets, const QString& except = {}, bool application = false, bool saved = false)
{
    const auto defaultName = application ? qtr("Default Profile") : qtr("Default Preset");
    if (name.isEmpty() || name.size() > 80 || (!saved && (name.compare(defaultName, Qt::CaseInsensitive) == 0
        || name.compare(application ? QStringLiteral("Default Profile") : QStringLiteral("Default Preset"), Qt::CaseInsensitive) == 0))
        || name.contains(QRegularExpression(QStringLiteral("[\\x00-\\x1f]")))) return false;
    return std::none_of(presets.cbegin(), presets.cend(), [&](const QVariant& row) {
        const auto preset = row.toMap();
        return preset.value(QStringLiteral("id")).toString() != except
            && preset.value(QStringLiteral("name")).toString().compare(name, Qt::CaseInsensitive) == 0;
    });
}

QString trackOption(const QString& name)
{
    if (name == QLatin1String("redesign-audio-track")) return QStringLiteral("audio-track");
    if (name == QLatin1String("redesign-sub-track")) return QStringLiteral("sub-track");
    return {};
}

es_format_category_e enabledTrackCategory(const QString& name)
{
    if (name == QLatin1String("audio")) return AUDIO_ES;
    if (name == QLatin1String("video")) return VIDEO_ES;
    if (name == QLatin1String("spu")) return SPU_ES;
    return UNKNOWN_ES;
}

QString languageOption(const QString& name)
{
    if (name == QLatin1String("redesign-audio-language")) return QStringLiteral("audio-language");
    if (name == QLatin1String("redesign-sub-language")) return QStringLiteral("sub-language");
    if (name == QLatin1String("redesign-menu-language")) return QStringLiteral("menu-language");
    return {};
}

QString languageNames(const QString& codes)
{
    if (codes.isEmpty()) return qtr("Automatic");
    QStringList names;
    for (const auto& code : codes.split(QLatin1Char(','))) {
        const auto bytes = code.trimmed().toUtf8();
        const auto* language = vlc_find_iso639(bytes.constData(), true);
        // Keep unrecognized codes and native fallback tokens editable.
        names.append(code == QLatin1String("system") ? qtr("System language")
            : language ? qtr(language->psz_eng_name) : code.trimmed());
    }
    return names.join(QStringLiteral(", "));
}

const iso639_lang_t* languageFromText(const QString& text)
{
    const auto bytes=text.toUtf8();
    if (const auto* language=vlc_find_iso639(bytes.constData(),true)) return language;
    size_t count;const auto* languages=vlc_iso639_languages(&count);
    for (size_t i=0;i<count;++i)
        if (text.compare(qtr(languages[i].psz_eng_name),Qt::CaseInsensitive)==0) return &languages[i];
    return nullptr;
}

QString languageCodes(const QString& input)
{
    QStringList result;
    for (const auto& token : input.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const auto bytes = token.trimmed().toUtf8();
        const auto* language = vlc_find_iso639(bytes.constData(), true);
        result.append(language ? qfu(*language->psz_iso639_1 ? language->psz_iso639_1 : language->psz_iso639_2T) : token.trimmed());
    }
    return result.join(QLatin1Char(','));
}

QVariantList languageChoices(const QString& current, bool allowSystem = true)
{
    const QVariantList languages = [] {
        QVariantList result;size_t count;
        const auto* records = vlc_iso639_languages(&count);
        for (size_t i = 0; i < count; ++i) {
            const auto& record = records[i];
            const auto code = qfu(*record.psz_iso639_1 ? record.psz_iso639_1 : record.psz_iso639_2T);
            result.append(QVariantMap{{"label", qtr(record.psz_eng_name)}, {"value", code}});
        }
        QCollator collator;collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(result.begin(),result.end(),[&](const auto& a,const auto& b) {
            return collator.compare(a.toMap().value("label").toString(),b.toMap().value("label").toString()) < 0;
        });
        return result;
    }();
    QVariantList result;
    if (allowSystem) result.append(QVariantMap{{"label",qtr("System language")},{"value",QStringLiteral("system")}});
    result.append(QVariantMap{{"label",qtr("Automatic")},{"value",QString()}});
    result.append(languages);
    if (!current.isEmpty() && (!allowSystem || current != QLatin1String("system"))
        && std::none_of(languages.cbegin(),languages.cend(),[&](const auto& row){return row.toMap().value("value").toString()==current;}))
        result.append(QVariantMap{{"label",!allowSystem && current == QLatin1String("system") ? current : languageNames(current)},{"value",current}});
    result.append(QVariantMap{{"label",qtr("Custom…")},{"value",QStringLiteral("__custom__")}});
    return result;
}

bool supportsWebRemote()
{
    // Lua's HTTP interface is a submodule: its shortcut need not be the
    // primary module name used by module_exists().
    static const bool available = [] {
        module_t** modules = nullptr;
        const ssize_t count = vlc_module_match("interface", "luahttp", true, &modules, nullptr);
        free(modules);
        return count > 0;
    }();
    return available;
}

QString categoryFor(const QString& name, int subcat)
{
    if (name.startsWith(QLatin1String("key-")) || name.startsWith(QLatin1String("global-key-"))
        || name.startsWith(QLatin1String("hotkeys-")) || name.startsWith(QLatin1String("mouse-"))
        || name.endsWith(QLatin1String("-jump-size"))) return QStringLiteral("controls");
    if (name.startsWith(QLatin1String("sub-")) || name.startsWith(QLatin1String("subsdec-"))
        || name.startsWith(QLatin1String("freetype-")) || name == QLatin1String("spu")) return QStringLiteral("subtitles");
    if (name.startsWith(QLatin1String("record")) || subcat / 100 == 5) return QStringLiteral("recording");
    if (name.contains(QLatin1String("caching")) || name.startsWith(QLatin1String("avcodec-"))
        || subcat == SUBCAT_INPUT_VCODEC || subcat == SUBCAT_INPUT_ACODEC) return QStringLiteral("performance");
    if (subcat == SUBCAT_ADVANCED_NETWORK || subcat == SUBCAT_INPUT_ACCESS
        || name.startsWith(QLatin1String("http-"))) return QStringLiteral("network");
    if (name == QLatin1String("recentplay-filter") || name == QLatin1String("save-recentplay")) return QStringLiteral("privacy");
    if (name.startsWith(QLatin1String("metadata-")) || name.contains(QLatin1String("scrobbler"))) return QStringLiteral("privacy");
    if (name.startsWith(QLatin1String("qt-"))) return QStringLiteral("interface");
    switch (subcat / 100) {
    case 2: return QStringLiteral("audio");
    case 3: return subcat == SUBCAT_VIDEO_SUBPIC ? QStringLiteral("subtitles") : QStringLiteral("video");
    case 4: return QStringLiteral("playback");
    case 7: return QStringLiteral("library");
    default: return QStringLiteral("general");
    }
}
}

RedesignPreferences::RedesignPreferences(QObject* parent) : QObject(parent)
{
    if (auto* translator = InterfaceTranslator::instance())
        connect(translator, &InterfaceTranslator::languageChanged, this, [this] {
            for (auto& entry : m_catalog) {
                const auto name = entry.name.toUtf8();
                const auto* config = config_FindConfig(name.constData());
                if (config && config->psz_text) entry.title = qtr(config->psz_text);
            }
            std::sort(m_catalog.begin(), m_catalog.end(), [](const Entry& a, const Entry& b) {
                return QString::localeAwareCompare(a.title, b.title) < 0;
            });
            ++m_translationRevision;
            emit translationRevisionChanged();
            ++m_revision;
            emit changed();
        }, Qt::QueuedConnection);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, [this]() { if (m_autoSave && dirty()) save(); });
    m_stereoTestTimer.setInterval(50);
    connect(&m_stereoTestTimer, &QTimer::timeout, this, &RedesignPreferences::updateStereoTest);
    m_pluginScanTimer.setSingleShot(true);
    m_pluginScanTimer.setInterval(30000);
    connect(&m_pluginScanTimer, &QTimer::timeout, this, [this] {
        m_pluginScanTimedOut = true;
        m_pluginScan.kill();
    });
    connect(&m_pluginScan, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finishPluginScan(qtr("Hakone cannot start the plugin scan. Make sure that the cache generator is installed."), true);
    });
    connect(&m_pluginScan, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            if (m_pluginScanTimedOut)
                finishPluginScan(qtr("The plugin scan timed out. Try again."), true);
            else if (code != 0 || status != QProcess::NormalExit)
                finishPluginScan(qtr("Hakone cannot update the plugin cache. Make sure that you have access to the folder. Try again."), true);
            else
                finishPluginScan(qtr("Hakone updated the plugin cache. Close Hakone. Start Hakone again to use plugin changes."), false);
        });
    m_pluginScan.setStandardOutputFile(QProcess::nullDevice());
    m_pluginScan.setStandardErrorFile(QProcess::nullDevice());
#ifdef _WIN32
    m_pluginScan.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
        // No global startup-feedback cursor for this background scan.
        args->startupInfo->dwFlags |= STARTF_FORCEOFFFEEDBACK;
    });
#endif
    size_t count = 0;
    module_t** modules = module_list_get(&count);
    for (size_t i = 0; i < count; ++i) {
        unsigned size = 0;
        module_config_t* configs = module_config_get(modules[i], &size);
        int subcat = SUBCAT_UNKNOWN;
        for (unsigned j = 0; j < size; ++j) {
            const module_config_t& cfg = configs[j];
            if (cfg.i_type == CONFIG_SUBCATEGORY) { subcat = cfg.value.i; continue; }
            if (!CONFIG_ITEM(cfg.i_type) || !cfg.psz_name || !cfg.psz_text
                || subcat == SUBCAT_HIDDEN || subcat == SUBCAT_UNKNOWN) continue;
            const QString name = qfu(cfg.psz_name);
            m_catalog.append({name, categoryFor(name, subcat), qtr(cfg.psz_text)});
        }
        module_config_free(configs);
    }
    module_list_free(modules);
    std::sort(m_catalog.begin(), m_catalog.end(), [](const Entry& a, const Entry& b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
}

RedesignPreferences::~RedesignPreferences()
{
    m_pluginScanTimer.stop();
    if (m_pluginScan.state() != QProcess::NotRunning) {
        m_pluginScan.disconnect(this);
        m_pluginScan.kill();
        m_pluginScan.waitForFinished(1000);
    }
    m_stereoTestTimer.stop();
    m_stereoTest.reset();
}

bool RedesignPreferences::scanPlugins()
{
    if (m_pluginScanRunning || !config_FindConfig("plugins-cache")) return false;
#ifdef _WIN32
    const char *filename = "vlc-cache-gen.exe";
#else
    const char *filename = "vlc-cache-gen";
#endif
    char *path = config_GetSysPath(VLC_PKG_LIBEXEC_DIR, nullptr);
    const auto program = path ? QDir(qfu(path)).filePath(qfu(filename)) : QString();
    free(path);
    m_pluginScanRunning = true;
    m_pluginScanFailed = m_pluginScanTimedOut = false;
    m_pluginScanMessage.clear();
    refresh();
    emit pluginScanChanged();
    m_pluginScanTimer.start();
    m_pluginScan.start(program, {QStringLiteral("--verify")});
    return true;
}

void RedesignPreferences::finishPluginScan(const QString& message, bool failed)
{
    m_pluginScanTimer.stop();
    m_pluginScanRunning = false;
    m_pluginScanFailed = failed;
    m_pluginScanMessage = message;
    refresh();
    emit pluginScanChanged();
}

bool RedesignPreferences::startStereoTest()
{
    stopStereoTest();
    if (!m_ctx || !value(QStringLiteral("audio")).toBool()) {
        finishStereoTest(qtr("Enable audio to run this test."));
        return false;
    }
    auto *output = vlc_player_aout_Hold(m_ctx->getIntf()->p_player);
    if (!output) {
        finishStereoTest(qtr("Hakone cannot start the audio test."));
        return false;
    }
    const bool muted = aout_MuteGet(output) > 0;
    const bool silent = aout_VolumeGet(output) == 0;
    if (muted || silent) {
        aout_Release(output);
        finishStereoTest(muted ? qtr("Unmute audio to run this test.")
                              : qtr("Increase the volume to run this test."));
        return false;
    }
    const int mode = isStaged(QStringLiteral("stereo-mode"))
        ? value(QStringLiteral("stereo-mode")).toInt() : var_GetInteger(output, "stereo-mode");
    auto test = std::make_unique<StereoTestPlayer>();
    const bool started = test->start(VLC_OBJECT(m_ctx->getIntf()), output, mode);
    aout_Release(output);
    if (!started) {
        test.reset();
        finishStereoTest(qtr("Hakone cannot start the audio test."));
        return false;
    }
    m_stereoTest = std::move(test);
    m_stereoTestMessage = qtr("Starting test");
    m_stereoTestElapsed.start();
    m_stereoTestTimer.start();
    emit stereoTestChanged();
    return true;
}

void RedesignPreferences::finishStereoTest(const QString& message)
{
    m_stereoTestTimer.stop();
    m_stereoTest.reset();
    m_stereoTestMessage = message;
    emit stereoTestChanged();
}

void RedesignPreferences::stopStereoTest()
{
    if (m_stereoTest || !m_stereoTestMessage.isEmpty()) finishStereoTest();
}

void RedesignPreferences::updateStereoTest()
{
    if (!m_stereoTest) return;
    const auto state = m_stereoTest->state();
    if (state == StereoTestPlayer::State::Failed || m_stereoTestElapsed.elapsed() > 8000) {
        finishStereoTest(qtr("Hakone cannot start the audio test."));
        return;
    }
    if (state == StereoTestPlayer::State::Complete) { finishStereoTest(); return; }
    const auto message = state == StereoTestPlayer::State::Left ? qtr("Left channel")
        : state == StereoTestPlayer::State::Right ? qtr("Right channel")
        : state == StereoTestPlayer::State::Starting ? qtr("Starting test") : QString();
    if (message != m_stereoTestMessage) {
        m_stereoTestMessage = message;
        emit stereoTestChanged();
    }
}

void RedesignPreferences::setAutoSave(bool enabled)
{
    if (m_autoSave == enabled) return;
    m_autoSave = enabled;
    if (!enabled) m_saveTimer.stop();
    else if (dirty()) m_saveTimer.start();
    emit autoSaveChanged();
}

void RedesignPreferences::setCtx(MainCtx* ctx)
{
    if (m_ctx == ctx) return;
    stopStereoTest();
    if (m_ctx) disconnect(m_ctx, &MainCtx::redesignPlayerPreferencesChanged, this, &RedesignPreferences::refresh);
    if (m_ctx) disconnect(m_ctx, &MainCtx::backdropBlurEnabledChanged, this, &RedesignPreferences::refresh);
    m_ctx = ctx;
    if (m_ctx) connect(m_ctx, &MainCtx::redesignPlayerPreferencesChanged, this, &RedesignPreferences::refresh);
    if (m_ctx) connect(m_ctx, &MainCtx::backdropBlurEnabledChanged, this, &RedesignPreferences::refresh);
    emit ctxChanged();
    refresh();
}

QVariant RedesignPreferences::read(const QByteArray& name)
{
    const auto* cfg = config_FindConfig(name.constData());
    if (!cfg) return {};
    switch (CONFIG_CLASS(cfg->i_type)) {
    case CONFIG_ITEM_BOOL: return bool(config_GetInt(name.constData()));
    case CONFIG_ITEM_INTEGER: return QVariant::fromValue<qlonglong>(config_GetInt(name.constData()));
    case CONFIG_ITEM_FLOAT: return config_GetFloat(name.constData());
    case CONFIG_ITEM_STRING: {
        char* text = config_GetPsz(name.constData());
        const QString result = qfu(text ? text : "");
        free(text);
        return result;
    }
    default: return {};
    }
}

void RedesignPreferences::write(const QByteArray& name, const QVariant& value)
{
    const auto* cfg = config_FindConfig(name.constData());
    if (!cfg) return;
    switch (CONFIG_CLASS(cfg->i_type)) {
    case CONFIG_ITEM_BOOL:
    case CONFIG_ITEM_INTEGER: config_PutInt(name.constData(), value.toLongLong()); break;
    case CONFIG_ITEM_FLOAT: config_PutFloat(name.constData(), value.toFloat()); break;
    case CONFIG_ITEM_STRING: config_PutPsz(name.constData(), value.toString().toUtf8().constData()); break;
    }
}

QString RedesignPreferences::audioOutputModule() const
{
    if (!m_ctx) return {};
    audio_output_t* output = vlc_player_aout_Hold(m_ctx->getIntf()->p_player);
    if (!output) return {};
    char* name = vlc_object_get_name(VLC_OBJECT(output));
    const QString result = qfu(name ? name : "");
    free(name); aout_Release(output);
    return result;
}

QVariantList RedesignPreferences::audioDeviceChoices() const
{
    QVariantList choices{QVariantMap{{QStringLiteral("label"), qtr("System default")}, {QStringLiteral("value"), QString()}}};
    if (!m_ctx) return choices;
    audio_output_t* output = vlc_player_aout_Hold(m_ctx->getIntf()->p_player);
    if (!output) return choices;
    char **ids = nullptr, **names = nullptr;
    const int count = aout_DevicesList(output, &ids, &names);
    for (int i = 0; i < count; ++i) {
        if (ids[i] && *ids[i]) choices.append(QVariantMap{{QStringLiteral("label"), qfu(names[i])}, {QStringLiteral("value"), qfu(ids[i])}});
        free(ids[i]); free(names[i]);
    }
    free(ids); free(names); aout_Release(output);
    return choices;
}

void RedesignPreferences::refresh()
{
    ++m_revision; emit changed();
}

QStringList RedesignPreferences::subtitleSearchLanguages() const
{
    QStringList result;
    for (const auto& token : value(QStringLiteral("sub-language")).toString().split(QLatin1Char(','))) {
        const auto code = token.trimmed().toLower().toUtf8();
        if (code.isEmpty()) continue;
        if (code == "any" || code == "none") break;
        const auto* language = code == "system" ? vlc_get_system_language()
            : vlc_find_iso639(code.constData(), true);
        if (!language) continue;
        const auto searchCode = qfu(*language->psz_iso639_1 ? language->psz_iso639_1 : language->psz_iso639_2T);
        if (!result.contains(searchCode)) result.append(searchCode);
    }
    return result;
}

void RedesignPreferences::setApplicationProfile(bool value)
{
    if (m_applicationProfile == value) return;
    discard();
    m_applicationProfile = value;
    ++m_revision; emit changed();
}

QString RedesignPreferences::presetStorageOption() const
{
    return m_applicationProfile ? applicationProfileOption : presetOption;
}

QVariantMap RedesignPreferences::playerPresetOptions() const
{
    return presetSnapshot(m_applicationProfile);
}

QVariantMap RedesignPreferences::presetSnapshot(bool application) const
{
    QVariantMap options;
    const auto names = application ? applicationOptions() : presetOptions();
    for (const auto& name : names) {
        if (!application && name.startsWith(QLatin1String("audio-spatial")) && !module_exists("spatialaudio")) continue;
        auto current = value(name);
        if (application && current.isNull() && config_GetType(name.toUtf8().constData()) == VLC_VAR_STRING)
            current = QStringLiteral("");
        if (current.isValid()) options.insert(name, current);
    }
    if (m_ctx) {
        auto* player = m_ctx->getIntf()->p_player;
        vlc_player_Lock(player);
        const bool hasVideo = vlc_player_IsStarted(player) && vlc_player_GetSelectedTrack(player, VIDEO_ES);
        if (vlc_player_IsStarted(player)) {
            // Shortcuts can change timing without writing the defaults. Capture
            // the values the viewer is using, while preserving staged edits.
            if (!isStaged(QStringLiteral("audio-desync")))
                options.insert(QStringLiteral("audio-desync"), QVariant::fromValue<qlonglong>(MS_FROM_VLC_TICK(vlc_player_GetAudioDelay(player))));
            if (!isStaged(QStringLiteral("redesign-subtitle-delay"))) {
                options.insert(QStringLiteral("sub-delay"), 0);
                options.insert(QStringLiteral("sub-delay-ms"), QVariant::fromValue<qlonglong>(MS_FROM_VLC_TICK(vlc_player_GetSubtitleDelay(player))));
            }
            if (!isStaged(QStringLiteral("sub-fps")))
                options.insert(QStringLiteral("sub-fps"), vlc_player_GetAssociatedSubsFPS(player));
            if (!isStaged(QStringLiteral("sub-text-scale")))
                options.insert(QStringLiteral("sub-text-scale"), vlc_player_GetSubtitleTextScale(player));
            if (!isStaged(QStringLiteral("sub-text-size-mode")))
                options.insert(QStringLiteral("sub-text-size-mode"), int(vlc_player_GetSubtitleTextSizeMode(player)));
            if (!isStaged(QStringLiteral("sub-text-size-pixels")))
                options.insert(QStringLiteral("sub-text-size-pixels"), vlc_player_GetSubtitleTextPixelSize(player));
        }
        vlc_player_Unlock(player);
        // Hold() can return the placeholder output when no video is selected.
        // Its cached defaults must not replace the current saved preferences.
        auto* output = hasVideo ? vlc_player_vout_Hold(player) : nullptr;
        if (output) {
            for (const auto& name : videoOutputOptions()) {
                const auto current = outputValue(output, name);
                if (!isStaged(name) && current.isValid()) options.insert(name, current);
            }
            vout_Release(output);
        }
    }
    return options;
}

QVariantMap RedesignPreferences::playerPresetState() const
{
    return presetState(m_applicationProfile);
}

QVariantMap RedesignPreferences::presetState(bool application) const
{
    const auto text = value(application ? applicationProfileOption : presetOption).toString().toUtf8();
    if (text.isEmpty()) {
        QVariantMap state{{QStringLiteral("version"), 1}, {QStringLiteral("selected"), QString()},
            {QStringLiteral("default"), presetSnapshot(application)}, {QStringLiteral("presets"), QVariantList()}};
        if (!application) return state;
        state.insert(QStringLiteral("scope"), QStringLiteral("application"));
        state.insert(QStringLiteral("defaultInterface"), encodeInterface(m_ctx ? m_ctx->profileSettings() : QVariantMap()));
        // The earlier main menu selected show presets. Keep their names and
        // identities while upgrading them to independent application profiles.
        if (!value(presetOption).toString().isEmpty()) {
            const auto legacy = presetState(false);
            if (legacy.isEmpty()) return {};
            const auto base = state.value(QStringLiteral("default")).toMap();
            auto snapshot = [&](const QVariantMap& presentation, const QString& selected) {
                auto options = base;
                for (auto it = presentation.cbegin(); it != presentation.cend(); ++it) options.insert(it.key(), it.value());
                auto shows = legacy; shows.insert(QStringLiteral("selected"), selected);
                options.insert(presetOption, QString::fromUtf8(QJsonDocument::fromVariant(shows).toJson(QJsonDocument::Compact)));
                return options;
            };
            state.insert(QStringLiteral("default"), snapshot(legacy.value(QStringLiteral("default")).toMap(), QString()));
            QVariantList profiles;
            for (const auto& row : legacy.value(QStringLiteral("presets")).toList()) {
                auto profile = row.toMap();
                profile.insert(QStringLiteral("options"), snapshot(profile.value(QStringLiteral("options")).toMap(), profile.value(QStringLiteral("id")).toString()));
                profile.insert(QStringLiteral("interface"), state.value(QStringLiteral("defaultInterface")));
                profiles.append(profile);
            }
            state.insert(QStringLiteral("presets"), profiles);
            state.insert(QStringLiteral("selected"), legacy.value(QStringLiteral("selected")));
        }
        return state;
    }
    if (text.size() > (application ? applicationProfileLimit : 1024 * 1024)) return {};
    const auto document = QJsonDocument::fromJson(text);
    if (!document.isObject()) return {};
    auto state = document.object().toVariantMap();
    if (state.value(QStringLiteral("version")).toInt() != 1
        || state.value(QStringLiteral("default")).metaType().id() != QMetaType::QVariantMap
        || state.value(QStringLiteral("presets")).metaType().id() != QMetaType::QVariantList) return {};
    QVariantMap interface;
    if (application && (state.value(QStringLiteral("scope")).toString() != QLatin1String("application")
        || !decodeInterface(state.value(QStringLiteral("defaultInterface")), interface))) return {};
    const auto validOptions = [application](const QVariant& value) {
        if (value.metaType().id() != QMetaType::QVariantMap) return false;
        const auto options = value.toMap();
        for (auto it = options.cbegin(); it != options.cend(); ++it) {
            static const QRegularExpression name(QStringLiteral("^[a-zA-Z0-9][a-zA-Z0-9_+.-]{0,255}$"));
            if ((application ? it.key() == applicationProfileOption || !name.match(it.key()).hasMatch() : !presetOptions().contains(it.key()))
                || it.value().isNull() || it.value().metaType().id() == QMetaType::QVariantMap
                || it.value().metaType().id() == QMetaType::QVariantList) return false;
        }
        return true;
    };
    if (!validOptions(state.value(QStringLiteral("default")))) return {};
    auto presets = state.value(QStringLiteral("presets")).toList();
    if (presets.size() > 128) return {};
    QSet<QString> ids;
    for (const auto& row : presets) {
        const auto preset = row.toMap();
        const auto id = preset.value(QStringLiteral("id")).toString();
        const auto name = preset.value(QStringLiteral("name")).toString();
        if (id.isEmpty() || ids.contains(id) || !validPresetName(name, presets, id, application, true)
            || !validOptions(preset.value(QStringLiteral("options")))
            || (application && !decodeInterface(preset.value(QStringLiteral("interface")), interface))) return {};
        ids.insert(id);
    }
    const auto selected = state.value(QStringLiteral("selected")).toString();
    if (!selected.isEmpty() && !ids.contains(selected)) return {};
    if (application) {
        // Older snapshots enumerated public module controls, so private saved
        // settings such as player layout were missing. New settings from a
        // different build also need a stable baseline rather than inheriting
        // the previously active profile's values.
        auto defaults = state.value(QStringLiteral("default")).toMap();
        for (const auto& name : applicationOptions()) {
            if (defaults.contains(name)) continue;
            const auto* cfg = config_FindConfig(name.toUtf8().constData());
            if (!cfg) continue;
            switch (CONFIG_CLASS(cfg->i_type)) {
            case CONFIG_ITEM_BOOL: defaults.insert(name, bool(cfg->orig.i)); break;
            case CONFIG_ITEM_INTEGER: defaults.insert(name, QVariant::fromValue<qlonglong>(cfg->orig.i)); break;
            case CONFIG_ITEM_FLOAT: defaults.insert(name, cfg->orig.f); break;
            case CONFIG_ITEM_STRING: defaults.insert(name, qfu(cfg->orig.psz ? cfg->orig.psz : "")); break;
            }
        }
        state.insert(QStringLiteral("default"), defaults);
        return state;
    }
    // Older presets stored fit=None for pixel zoom but omitted autoscale.
    // Normalize each snapshot before merging a named preset with defaults.
    const auto normalize = [](QVariantMap options) {
        if (!options.contains(QStringLiteral("autoscale")) && options.contains(QStringLiteral("fit")))
            options.insert(QStringLiteral("autoscale"), options.value(QStringLiteral("fit")).toInt() != VLC_VIDEO_FIT_NONE);
        // Presets made before layout editing use the original player layout.
        if (!options.contains(QStringLiteral("qt-player-layout")))
            options.insert(QStringLiteral("qt-player-layout"), QString());
        // Resolve each older snapshot before it is merged with newer defaults.
        if (options.contains(QStringLiteral("freetype-background-opacity"))
            && !options.contains(QStringLiteral("freetype-background-mode")))
            options.insert(QStringLiteral("freetype-background-mode"), -1);
        return options;
    };
    state.insert(QStringLiteral("default"), normalize(state.value(QStringLiteral("default")).toMap()));
    for (auto& row : presets) {
        auto preset = row.toMap();
        preset.insert(QStringLiteral("options"), normalize(preset.value(QStringLiteral("options")).toMap()));
        row = preset;
    }
    state.insert(QStringLiteral("presets"), presets);
    return state;
}

QVariantMap RedesignPreferences::captureApplicationProfile(QVariantMap state) const
{
    if (state.isEmpty()) return state;
    const auto selected = state.value(QStringLiteral("selected")).toString();
    const auto current = presetSnapshot(true);
    const auto ui = encodeInterface(m_ctx ? m_ctx->profileSettings() : QVariantMap());
    if (selected.isEmpty()) {
        auto options = state.value(QStringLiteral("default")).toMap();
        for (auto it = current.cbegin(); it != current.cend(); ++it) options.insert(it.key(), it.value());
        state.insert(QStringLiteral("default"), options);
        state.insert(QStringLiteral("defaultInterface"), ui);
    } else {
        auto profiles = state.value(QStringLiteral("presets")).toList();
        for (auto& row : profiles) {
            auto profile = row.toMap();
            if (profile.value(QStringLiteral("id")).toString() != selected) continue;
            auto options = profile.value(QStringLiteral("options")).toMap();
            for (auto it = current.cbegin(); it != current.cend(); ++it) options.insert(it.key(), it.value());
            profile.insert(QStringLiteral("options"), options);
            profile.insert(QStringLiteral("interface"), ui); row = profile;
        }
        state.insert(QStringLiteral("presets"), profiles);
    }
    return state;
}

bool RedesignPreferences::updateActiveApplicationProfile()
{
    if (m_edits.contains(applicationProfileOption) || value(applicationProfileOption).toString().isEmpty()) return true;
    auto state = presetState(true);
    // Keep an unreadable archive intact. Ordinary settings remain editable.
    if (state.isEmpty()) return true;
    const auto selected = state.value(QStringLiteral("selected")).toString();
    auto update = [&](QVariantMap options) {
        for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it)
            if (config_IsPersistent(it.key().toUtf8().constData())) options.insert(it.key(), it.value());
        return options;
    };
    const auto ui = encodeInterface(m_ctx ? m_ctx->profileSettings() : QVariantMap());
    if (selected.isEmpty()) {
        state.insert(QStringLiteral("default"), update(state.value(QStringLiteral("default")).toMap()));
        state.insert(QStringLiteral("defaultInterface"), ui);
    } else {
        auto profiles = state.value(QStringLiteral("presets")).toList();
        for (auto& row : profiles) {
            auto profile = row.toMap();
            if (profile.value(QStringLiteral("id")).toString() != selected) continue;
            profile.insert(QStringLiteral("options"), update(profile.value(QStringLiteral("options")).toMap()));
            profile.insert(QStringLiteral("interface"), ui); row = profile;
        }
        state.insert(QStringLiteral("presets"), profiles);
    }
    const auto data = QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact);
    if (data.size() > applicationProfileLimit) return playerPresetError(qtr("Hakone cannot save these profiles. Remove unused profiles."));
    m_edits.insert(applicationProfileOption, QString::fromUtf8(data));
    return true;
}

QVariantList RedesignPreferences::playerPresets() const
{
    QVariantList result;
    for (const auto& row : playerPresetState().value(QStringLiteral("presets")).toList()) {
        const auto preset = row.toMap();
        result.append(QVariantMap{{QStringLiteral("id"), preset.value(QStringLiteral("id"))},
            {QStringLiteral("name"), preset.value(QStringLiteral("name"))}});
    }
    return result;
}

QString RedesignPreferences::selectedPlayerPreset() const
{
    return playerPresetState().value(QStringLiteral("selected")).toString();
}

bool RedesignPreferences::playerPresetError(const QString& message)
{
    m_error = message; ++m_revision; emit changed(); return false;
}

bool RedesignPreferences::storePlayerPresetState(const QVariantMap& state)
{
    const auto data = QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact);
    if (data.size() > (m_applicationProfile ? applicationProfileLimit : 1024 * 1024)) {
        cancelPlayerPreset();
        return playerPresetError(qtr("Hakone cannot save these profiles. Remove unused profiles."));
    }
    if (!m_presetTransaction) {
        m_editsBeforePreset = m_edits;
        m_libraryStartupBeforePreset = m_libraryStartupEdited;
        m_autoPreparseBeforePreset = m_autoPreparseEdited;
        m_singleInstanceBeforePreset = m_singleInstanceEdited;
        m_presetTransaction = true;
    }
    m_edits.insert(presetStorageOption(), QString::fromUtf8(data));
    ++m_revision; emit changed();
    return save();
}

void RedesignPreferences::cancelPlayerPreset()
{
    if (!m_presetTransaction) return;
    m_saveTimer.stop();
    m_edits = std::move(m_editsBeforePreset);
    m_libraryStartupEdited = m_libraryStartupBeforePreset;
    m_autoPreparseEdited = m_autoPreparseBeforePreset;
    m_singleInstanceEdited = m_singleInstanceBeforePreset;
    m_presetTransaction = false;
    m_profileInterfacePending = false; m_profileInterface.clear();
    if (m_invalid.isEmpty()) m_error.clear();
    ++m_revision; emit changed();
    if (m_autoSave && dirty()) m_saveTimer.start();
}

bool RedesignPreferences::createPlayerPreset(const QString& proposed)
{
    auto state = playerPresetState();
    if (m_applicationProfile) state = captureApplicationProfile(state);
    if (state.isEmpty()) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
    auto presets = state.value(QStringLiteral("presets")).toList();
    const auto name = proposed.trimmed();
    if (!validPresetName(name, presets, {}, m_applicationProfile)) return playerPresetError(m_applicationProfile ? qtr("Enter a profile name with 1 to 80 characters. Use a different name for each profile.") : qtr("Enter a unique preset name with 1 to 80 characters."));
    if (presets.size() >= 128) return playerPresetError(m_applicationProfile ? qtr("Delete a profile before you add another profile.") : qtr("Delete a preset before you add another preset."));
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QVariantMap created{{QStringLiteral("id"), id}, {QStringLiteral("name"), name}, {QStringLiteral("options"), playerPresetOptions()}};
    if (m_applicationProfile) created.insert(QStringLiteral("interface"), encodeInterface(m_ctx ? m_ctx->profileSettings() : QVariantMap()));
    presets.append(created);
    state.insert(QStringLiteral("presets"), presets);
    state.insert(QStringLiteral("selected"), id);
    return storePlayerPresetState(state);
}

bool RedesignPreferences::selectPlayerPreset(const QString& id)
{
    // Finish any edits to the active preset before switching to another one.
    if (dirty() && !save()) return false;
    auto state = playerPresetState();
    if (m_applicationProfile) state = captureApplicationProfile(state);
    if (state.isEmpty()) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
    QVariantMap options = state.value(QStringLiteral("default")).toMap();
    QVariant interface = state.value(QStringLiteral("defaultInterface"));
    if (!id.isEmpty()) {
        const auto presets = state.value(QStringLiteral("presets")).toList();
        const auto it = std::find_if(presets.cbegin(), presets.cend(), [&](const QVariant& row) { return row.toMap().value(QStringLiteral("id")).toString() == id; });
        if (it == presets.cend()) return playerPresetError(m_applicationProfile ? qtr("This profile is no longer available.") : qtr("This preset is no longer available."));
        interface = it->toMap().value(QStringLiteral("interface"));
        const auto saved = it->toMap().value(QStringLiteral("options")).toMap();
        for (auto it = saved.cbegin(); it != saved.cend(); ++it) options.insert(it.key(), it.value());
        // A default hint cannot describe an older preset's native transform.
        if (!saved.contains(QStringLiteral("qt-video-transform-state")))
            options.insert(QStringLiteral("qt-video-transform-state"), QString());
    }
    if (!stagePlayerPresetOptions(options)) return false;
    if (m_applicationProfile) {
        if (!decodeInterface(interface, m_profileInterface)) { cancelPlayerPreset(); return playerPresetError(qtr("Hakone cannot read the saved profiles.")); }
        m_profileInterfacePending = true;
    }
    state.insert(QStringLiteral("selected"), id);
    return storePlayerPresetState(state);
}

bool RedesignPreferences::stagePlayerPresetOptions(const QVariantMap& options)
{
    // Validate before changing the transaction. A preset from another build can
    // contain options for modules that are not installed here; retain those in
    // the manifest and apply the options that this build supports.
    QSignalBlocker notifications(this);
    const auto edits = m_edits;
    const auto invalid = m_invalid;
    const bool libraryStartup = m_libraryStartupEdited, autoPreparse = m_autoPreparseEdited, singleInstance = m_singleInstanceEdited;
    // Older presets have native transforms but no logical Rotate/Flip hint.
    // Do not let the previous preset's ambiguous pair label their output.
    if (!options.contains(QStringLiteral("qt-video-transform-state")))
        stage(QStringLiteral("qt-video-transform-state"), QString());
    for (auto it = options.cbegin(); it != options.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("audio-spatial")) && !module_exists("spatialaudio")) continue;
        if (!config_FindConfig(it.key().toUtf8().constData())
            || (m_applicationProfile && !config_IsPersistent(it.key().toUtf8().constData()))) continue;
        if (m_applicationProfile && !presetOptions().contains(it.key()) && !isStaged(it.key()) && value(it.key()) == it.value()) continue;
        if (!stage(it.key(), it.value())) {
            m_edits = edits; m_invalid = invalid; m_saveTimer.stop();
            m_libraryStartupEdited = libraryStartup; m_autoPreparseEdited = autoPreparse; m_singleInstanceEdited = singleInstance;
            notifications.unblock();
            return playerPresetError(m_applicationProfile ? qtr("Hakone cannot apply this profile. Make sure that its saved settings are correct.") : qtr("Hakone cannot apply this preset. Make sure that its saved settings are correct."));
        }
    }
    // Some editors stage dependent settings (for example, an equalizer curve).
    // Restore the validated snapshot exactly after those editor side effects.
    for (auto it = options.cbegin(); it != options.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("audio-spatial")) && !module_exists("spatialaudio")) continue;
        if (!config_FindConfig(it.key().toUtf8().constData())
            || (m_applicationProfile && !config_IsPersistent(it.key().toUtf8().constData()))) continue;
        if (m_applicationProfile && !presetOptions().contains(it.key()) && !isStaged(it.key()) && value(it.key()) == it.value()) continue;
        // Reapply even an unchanged default: a shortcut may have changed the
        // live output since this configuration was saved.
        m_edits.insert(it.key(), it.value());
    }
    if (!m_presetTransaction) {
        m_editsBeforePreset = edits;
        m_libraryStartupBeforePreset = libraryStartup;
        m_autoPreparseBeforePreset = autoPreparse;
        m_singleInstanceBeforePreset = singleInstance;
        m_presetTransaction = true;
    }
    return true;
}

bool RedesignPreferences::renamePlayerPreset(const QString& id, const QString& proposed)
{
    auto state = playerPresetState();
    if (m_applicationProfile) state = captureApplicationProfile(state);
    if (state.isEmpty()) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
    auto presets = state.value(QStringLiteral("presets")).toList();
    const auto name = proposed.trimmed();
    if (!validPresetName(name, presets, id, m_applicationProfile)) return playerPresetError(m_applicationProfile ? qtr("Enter a profile name with 1 to 80 characters. Use a different name for each profile.") : qtr("Enter a unique preset name with 1 to 80 characters."));
    for (auto& row : presets) {
        auto preset = row.toMap();
        if (preset.value(QStringLiteral("id")).toString() != id) continue;
        preset.insert(QStringLiteral("name"), name); row = preset;
        state.insert(QStringLiteral("presets"), presets);
        return storePlayerPresetState(state);
    }
    return playerPresetError(m_applicationProfile ? qtr("This profile is no longer available.") : qtr("This preset is no longer available."));
}

bool RedesignPreferences::duplicatePlayerPreset(const QString& id, const QString& proposed)
{
    auto state = playerPresetState();
    if (m_applicationProfile) state = captureApplicationProfile(state);
    if (state.isEmpty()) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
    auto presets = state.value(QStringLiteral("presets")).toList();
    const auto name = proposed.trimmed();
    if (!validPresetName(name, presets, {}, m_applicationProfile)) return playerPresetError(m_applicationProfile ? qtr("Enter a profile name with 1 to 80 characters. Use a different name for each profile.") : qtr("Enter a unique preset name with 1 to 80 characters."));
    if (presets.size() >= 128) return playerPresetError(m_applicationProfile ? qtr("Delete a profile before you add another profile.") : qtr("Delete a preset before you add another preset."));
    for (const auto& row : std::as_const(presets)) {
        auto preset = row.toMap();
        if (preset.value(QStringLiteral("id")).toString() != id) continue;
        preset.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        preset.insert(QStringLiteral("name"), name);
        presets.append(preset);
        state.insert(QStringLiteral("presets"), presets);
        return storePlayerPresetState(state);
    }
    return playerPresetError(m_applicationProfile ? qtr("This profile is no longer available.") : qtr("This preset is no longer available."));
}

bool RedesignPreferences::deletePlayerPreset(const QString& id)
{
    auto state = playerPresetState();
    if (m_applicationProfile) state = captureApplicationProfile(state);
    if (state.isEmpty()) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
    auto presets = state.value(QStringLiteral("presets")).toList();
    const auto it = std::find_if(presets.begin(), presets.end(), [&](const QVariant& row) { return row.toMap().value(QStringLiteral("id")).toString() == id; });
    if (it == presets.end()) return playerPresetError(m_applicationProfile ? qtr("This profile is no longer available.") : qtr("This preset is no longer available."));
    presets.erase(it);
    // Deleting the active preset restores the user's default presentation in
    // the same transaction. Deleting another preset does not affect playback.
    if (state.value(QStringLiteral("selected")).toString() == id) {
        if (!stagePlayerPresetOptions(state.value(QStringLiteral("default")).toMap())) return false;
        if (m_applicationProfile) {
            if (!decodeInterface(state.value(QStringLiteral("defaultInterface")), m_profileInterface)) { cancelPlayerPreset(); return playerPresetError(qtr("Hakone cannot read the saved profiles.")); }
            m_profileInterfacePending = true;
        }
        state.insert(QStringLiteral("selected"), QString());
    }
    state.insert(QStringLiteral("presets"), presets);
    return storePlayerPresetState(state);
}

QVariant RedesignPreferences::value(const QString& name) const
{
    if (name == QLatin1String("redesign-fullscreen-sensitivity")) {
        const auto pixels = value(QStringLiteral("qt-fs-sensitivity")).toLongLong();
        const auto nearest = std::min_element(std::begin(fullscreenSensitivityLevels), std::end(fullscreenSensitivityLevels),
            [pixels](int a, int b) { return std::abs(pixels - a) < std::abs(pixels - b); });
        return int(nearest - std::begin(fullscreenSensitivityLevels));
    }
    if (name == QLatin1String("language")) {
        QString selected = m_edits.contains(name) ? m_edits.value(name).toString() : read("language").toString();
        if (selected.isEmpty()) {
            if (m_ctx && var_InheritBool(m_ctx->getIntf(), "ignore-config")) return QStringLiteral("auto");
            const char* args[] = {"--language="};
            char* legacy = config_GetInterfaceLanguage(1, args);
            selected = qfu(legacy ? legacy : "auto");
            free(legacy);
        }
        return selected.isEmpty() ? QStringLiteral("auto") : selected;
    }
    if (name == QLatin1String("redesign-postprocessing")) {
        const auto *cfg = config_FindConfig("postproc-q");
        if (!cfg) return {};
        return vlc::postprocessing::quality(value(QStringLiteral("video-filter")).toString(),
            value(QStringLiteral("postproc-q")).toInt(), cfg->max.i);
    }
    if (name == QLatin1String("redesign-hardware-decoding")) {
        const auto device = value(QStringLiteral("dec-dev")).toString();
        if (!value(QStringLiteral("hw-dec")).toBool() || device == QLatin1String("none"))
            return QStringLiteral("none");
        return device.isEmpty() ? QStringLiteral("any") : device;
    }
    if (name == QLatin1String("redesign-subtitle-encoding"))
        return QString((value(QStringLiteral("subsdec-autodetect-utf8")).toBool()
            ? QStringLiteral("auto:") : QStringLiteral("fixed:"))
            + value(QStringLiteral("subsdec-encoding")).toString());
    if (name == QLatin1String("redesign-buffering")) {
        if (value(QStringLiteral("qt-buffering-custom")).toBool()) return 0;
        for (int level : bufferingLevels)
            if (std::all_of(std::begin(bufferingOptions), std::end(bufferingOptions), [&](const char *option) {
                return value(qfu(option)).toLongLong() == bufferingValue(option, level);
            })) return level;
        return 0;
    }
    if (name == QLatin1String("redesign-video-zoom"))
        return value(QStringLiteral("autoscale")).toBool()
            ? -std::max(1, value(QStringLiteral("fit")).toInt()) : value(QStringLiteral("zoom"));
    if (name == QLatin1String("redesign-snapshot-size")) {
        const int width = value(QStringLiteral("snapshot-width")).toInt();
        const int height = value(QStringLiteral("snapshot-height")).toInt();
        if (width == -1 && height == -1) return QStringLiteral("source");
        if (height == 0 && (width == 640 || width == 1280 || width == 1920)) return QString::number(width);
        return QStringLiteral("custom");
    }
    if (name == QLatin1String("redesign-audio-passthrough")) {
        const auto option = audioPassthroughOption(audioOutputModule());
        if (option.isEmpty()) return {};
        const auto native = value(option);
        return option == QLatin1String("mmdevice-passthrough") ? QVariant(native.toInt() != 0) : native;
    }
    if (name == QLatin1String("redesign-resampling-quality")) {
        const auto* quality = resamplingQuality(value(QStringLiteral("audio-resampler")).toString());
        if (!quality) return QStringLiteral("custom");
        const int current = value(qfu(quality->option)).toInt();
        return current == quality->low ? QStringLiteral("low")
            : current == quality->balanced ? QStringLiteral("balanced")
            : current == quality->high ? QStringLiteral("high") : QStringLiteral("custom");
    }
    if (name == QLatin1String("redesign-subtitle-background")) {
        const int mode = value(QStringLiteral("freetype-background-mode")).toInt();
        return mode < 0 ? int(value(QStringLiteral("freetype-background-opacity")).toInt() > 0) : mode;
    }
    if (name == QLatin1String("redesign-subtitle-background-opacity"))
        return value(QStringLiteral("freetype-background-opacity"));
    if (name == QLatin1String("redesign-subtitle-font-size"))
        return value(value(QStringLiteral("sub-text-size-mode")).toInt() == 1
            ? QStringLiteral("sub-text-size-pixels") : QStringLiteral("sub-text-scale"));
    if (name == QLatin1String("redesign-subtitle-duration"))
        return value(QStringLiteral("filter:sub-filter:subsdelay")).toBool() ? value(QStringLiteral("subsdelay-factor")) : QVariant(0.0);
    if (name == QLatin1String("redesign-subtitle-delay"))
        return value(QStringLiteral("sub-delay")).toLongLong() * 100 + value(QStringLiteral("sub-delay-ms")).toLongLong();
    if (name == QLatin1String("redesign-equalizer-preset")) {
        if (!value(QStringLiteral("filter:audio-filter:equalizer")).toBool()) return QString();
        const auto preset = value(QStringLiteral("equalizer-preset")).toString();
        return preset.isEmpty() ? QStringLiteral("custom") : preset;
    }
    if (const auto option = trackOption(name); !option.isEmpty())
        return value(option).toLongLong() + 1;
    if (const auto option = languageOption(name); !option.isEmpty())
        return languageCodes(value(option).toString());
    if (name == QLatin1String("redesign-audio-device")) {
        const QString module = audioOutputModule();
        const QString configured = value(QStringLiteral("audio-device-default")).toString();
        const QString driver = value(QStringLiteral("audio-device-default-module")).toString();
        if (!configured.isEmpty() && (driver.isEmpty() || driver == module)) return configured;
        const QString legacyName = module + QStringLiteral("-audio-device");
        const auto* legacy = config_FindConfig(legacyName.toUtf8().constData());
        if (legacy && CONFIG_CLASS(legacy->i_type) == CONFIG_ITEM_STRING) {
            const QString selected = value(legacyName).toString();
            if (selected != qfu(legacy->orig.psz ? legacy->orig.psz : "")) return selected;
        }
        return QString();
    }
    if (name == QLatin1String("redesign-startup-volume")) {
        const int configured = value(QStringLiteral("audio-startup-volume")).toInt();
        if (configured >= 0) return configured;
        const QString module = audioOutputModule();
        const bool gain = module == QLatin1String("alsa") || module == QLatin1String("jack") || module == QLatin1String("kai");
        const QString legacyName = module + (gain ? QStringLiteral("-gain") : QStringLiteral("-volume"));
        if (config_FindConfig(legacyName.toUtf8().constData())) {
            double volume = value(legacyName).toDouble();
            if (gain) volume = std::cbrt(volume);
            if (module == QLatin1String("auhal")) volume /= AOUT_VOLUME_DEFAULT;
            if (std::isfinite(volume)) return std::clamp(int(std::lround(volume * 100)), 0, 300);
        }
        return 100;
    }

    if (name == QLatin1String("redesign-log-details"))
        return vlc::filelog::selection(value(QStringLiteral("log-level")).toString(),
            value(QStringLiteral("file-logging")).toBool(), value(QStringLiteral("log-verbose")).toLongLong(),
            value(QStringLiteral("verbose")).toLongLong());

    if (name == QLatin1String("redesign-web-remote")) {
        const auto interfaces = value(QStringLiteral("extraintf")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
        return std::any_of(interfaces.cbegin(), interfaces.cend(), [](const auto& entry) {
            const auto module = entry.section(QLatin1Char('{'), 0, 0);
            return module == QLatin1String("http") || module == QLatin1String("luahttp");
        });
    }
    if (name == QLatin1String("redesign-repeat"))
        return value(QStringLiteral("repeat")).toBool() ? 1 : value(QStringLiteral("loop")).toBool() ? 2 : 0;
    if (name == QLatin1String("redesign-after-playback"))
        return value(QStringLiteral("play-and-exit")).toBool() ? 1 : value(QStringLiteral("play-and-pause")).toBool() ? 2 : 0;
    if (name.startsWith(QLatin1String("filter:"))) {
        const auto parts = name.split(QLatin1Char(':'));
        return parts.size() == 3 && value(parts[1]).toString().split(QLatin1Char(':')).contains(parts[2]);
    }

    const auto it = m_edits.constFind(name);
    if (it != m_edits.cend()) return *it;
    if (m_ctx && (name == QLatin1String("audio-visual") || name == QLatin1String("effect-list")))
        return visualizationValue(m_ctx->getIntf(), name.toUtf8().constData());
    if (m_ctx && (name == QLatin1String("qt-error-dialogs") || name == QLatin1String("qt-minor-warnings") || name == QLatin1String("qt-verbose") || name == QLatin1String("playlist-tree") || name == QLatin1String("qt-video-autoresize") || name == QLatin1String("qt-embedded-open")))
        return var_InheritBool(m_ctx->getIntf(), name.toUtf8().constData());
    if (m_ctx && name == QLatin1String("qt-system-tray"))
        return m_ctx->systrayRequested();
    if (m_ctx && (name == QLatin1String("qt-notification") || name == QLatin1String("qt-notification-timeout")))
        return QVariant::fromValue<qlonglong>(var_InheritInteger(m_ctx->getIntf(), name.toUtf8().constData()));
    if (m_ctx && (name == QLatin1String("qt-opacity") || name == QLatin1String("qt-fs-opacity")))
        return var_InheritFloat(m_ctx->getIntf(), name.toUtf8().constData());
    if (m_ctx && name == QLatin1String("qt-fs-sensitivity"))
        return QVariant::fromValue<qlonglong>(var_InheritInteger(m_ctx->getIntf(), "qt-fs-sensitivity"));
    if (m_ctx && name == QLatin1String("qt-backdrop-blur"))
        return m_ctx->backdropBlurRequested();
    if (m_ctx && name == QLatin1String("qt-compact-menus"))
        return m_ctx->compactMenus();
    if (m_ctx && name == QLatin1String("qt-discover-cast-devices"))
        return m_ctx->getIntf()->p_mainPlayerController->getRendererManager()->discoveryEnabled();
    if (m_ctx && name == QLatin1String("recursive")) {
        switch (directoryDepth(m_ctx)) {
        case VLC_PLAYLIST_PREPARSING_ENABLED: return QStringLiteral("none");
        case VLC_PLAYLIST_PREPARSING_COLLAPSE: return QStringLiteral("collapse");
        case VLC_PLAYLIST_PREPARSING_RECURSIVE: return QStringLiteral("expand");
        case VLC_PLAYLIST_PREPARSING_DISABLED: break;
        }
    }
    return read(name.toUtf8());
}

QVariant RedesignPreferences::videoValue(const QString& name) const
{
    if (!m_ctx || m_edits.contains(name) || (!videoOutputOptions().contains(name) && name != QLatin1String("sub-filter")))
        return value(name);
    auto* player = m_ctx->getIntf()->p_player;
    vlc_player_Lock(player);
    const bool hasVideo = vlc_player_IsStarted(player) && vlc_player_GetSelectedTrack(player, VIDEO_ES);
    auto* output = hasVideo ? vlc_player_vout_Hold(player) : nullptr;
    vlc_player_Unlock(player);
    if (!output) return value(name);
    const auto current = outputValue(output, name);
    vout_Release(output);
    return current.isValid() ? current : value(name);
}

bool RedesignPreferences::isStaged(const QString& name) const
{
    if (name == QLatin1String("redesign-log-details"))
        return m_edits.contains(QStringLiteral("log-level"))
            || (value(QStringLiteral("log-level")).toString().isEmpty()
                && (m_edits.contains(QStringLiteral("file-logging")) || m_edits.contains(QStringLiteral("log-verbose"))
                    || m_edits.contains(QStringLiteral("verbose"))));
    if (name == QLatin1String("redesign-postprocessing"))
        return m_edits.contains(QStringLiteral("postproc-q")) || m_edits.contains(QStringLiteral("video-filter"));
    if (name == QLatin1String("redesign-hardware-decoding"))
        return m_edits.contains(QStringLiteral("hw-dec")) || m_edits.contains(QStringLiteral("dec-dev"));
    if (name == QLatin1String("redesign-subtitle-encoding"))
        return m_edits.contains(QStringLiteral("subsdec-encoding"))
            || m_edits.contains(QStringLiteral("subsdec-autodetect-utf8"));
    if (name == QLatin1String("redesign-buffering"))
        return m_edits.contains(QStringLiteral("qt-buffering-custom"))
            || std::any_of(std::begin(bufferingOptions), std::end(bufferingOptions), [&](const char *option) { return m_edits.contains(qfu(option)); });
    if (name == QLatin1String("redesign-subtitle-background"))
        return m_edits.contains(QStringLiteral("freetype-background-mode"));
    if (name == QLatin1String("redesign-subtitle-background-opacity"))
        return m_edits.contains(QStringLiteral("freetype-background-mode")) || m_edits.contains(QStringLiteral("freetype-background-opacity"));
    if (name == QLatin1String("redesign-video-zoom"))
        return m_edits.contains(QStringLiteral("zoom")) || m_edits.contains(QStringLiteral("fit")) || m_edits.contains(QStringLiteral("autoscale"));
    if (name == QLatin1String("redesign-snapshot-size"))
        return m_edits.contains(QStringLiteral("snapshot-width")) || m_edits.contains(QStringLiteral("snapshot-height"));
    if (name == QLatin1String("redesign-audio-passthrough"))
        return m_edits.contains(audioPassthroughOption(audioOutputModule()));
    if (name == QLatin1String("redesign-resampling-quality")) {
        if (m_edits.contains(QStringLiteral("audio-resampler"))) return true;
        for (const auto& quality : resamplingQualities)
            if (m_edits.contains(qfu(quality.option))) return true;
        return false;
    }
    if (name == QLatin1String("redesign-subtitle-font-size"))
        return m_edits.contains(QStringLiteral("sub-text-scale")) || m_edits.contains(QStringLiteral("sub-text-size-pixels"))
            || m_edits.contains(QStringLiteral("sub-text-size-mode"));
    if (name == QLatin1String("redesign-subtitle-delay"))
        return m_edits.contains(QStringLiteral("sub-delay")) || m_edits.contains(QStringLiteral("sub-delay-ms"));
    return m_edits.contains(name);
}

bool RedesignPreferences::clearHistory()
{
    if (!m_ctx || !m_ctx->getMediaLibrary()) return false;
    m_ctx->getMediaLibrary()->runOnMLThread<bool>(this,
        [](vlc_medialibrary_t* ml, bool& success) {
            success = vlc_ml_clear_history(ml, VLC_ML_HISTORY_TYPE_GLOBAL) == VLC_SUCCESS;
        },
        [this](quint64, bool& success) { emit historyCleared(success); });
    return true;
}

QVariantMap RedesignPreferences::describe(const QString& name) const
{
    if (name == QLatin1String("redesign-fullscreen-sensitivity")) {
        if (!config_FindConfig("qt-fs-sensitivity")) return {};
        return {{QStringLiteral("name"), name}, {QStringLiteral("kind"), QStringLiteral("integer")},
            {QStringLiteral("title"), qtr("Fullscreen controls sensitivity")},
            {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 6},
            {QStringLiteral("defaultValue"), 3}, {QStringLiteral("value"), value(name)},
            {QStringLiteral("help"), qtr("Move the pointer near the top or bottom edge to show the fullscreen controls. A higher sensitivity needs less movement.")}};
    }
    if (name == QLatin1String("redesign-log-details")) {
        auto description = describe(QStringLiteral("log-level"));
        if (description.isEmpty()) return {};
        auto choices = description.value(QStringLiteral("choices")).toList();
        choices.erase(std::remove_if(choices.begin(), choices.end(), [](const auto& choice) {
            return choice.toMap().value(QStringLiteral("value")).toString().isEmpty();
        }), choices.end());
        const auto current = value(name).toString();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& choice) {
            return choice.toMap().value(QStringLiteral("value")).toString() == current;
        }))
            choices.append(QVariantMap{{QStringLiteral("value"), current}, {QStringLiteral("label"),
                current == QLatin1String("native-info-errors") ? qtr("Errors and information") : qtr("Saved value (%1)").arg(current)}});
        description.insert(QStringLiteral("name"), name);
        description.insert(QStringLiteral("title"), qtr("Log details"));
        description.insert(QStringLiteral("editor"), QStringLiteral("choice"));
        description.insert(QStringLiteral("choices"), choices);
        description.insert(QStringLiteral("value"), current);
        description.insert(QStringLiteral("defaultValue"), QStringLiteral("errors"));
        description.insert(QStringLiteral("help"), qtr("Select the messages to save in the log file. After a change, close Hakone. Start Hakone again."));
        return description;
    }
    if (!VLC_SKINS_ENABLED && name == QLatin1String("skins2-last"))
        return {};
    if (name == QLatin1String("redesign-clear-history"))
        return {{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("editor"), QStringLiteral("action")},
            {QStringLiteral("buttonLabel"), qtr("Clear")},
            {QStringLiteral("supported"), m_ctx && m_ctx->getMediaLibrary()},
            {QStringLiteral("help"), qtr("Remove the playback history for local files and network streams. Keep media files and playlists.")}};
    if (name == QLatin1String("redesign-rescan-plugins")) {
        if (!config_FindConfig("plugins-cache")) return {};
        return {{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("editor"), QStringLiteral("action")},
            {QStringLiteral("buttonLabel"), m_pluginScanRunning ? qtr("Scanning…") : qtr("Rescan")},
            {QStringLiteral("supported"), !m_pluginScanRunning},
            {QStringLiteral("help"), qtr("Hakone examines plugin files and updates the plugin cache. After the scan, close Hakone. Start Hakone again to use plugin changes.")}};
    }
    if (name == QLatin1String("redesign-postprocessing")) {
        auto description = describe(QStringLiteral("postproc-q"));
        if (description.isEmpty()) return {};
        const int maximum = description.value(QStringLiteral("maximum")).toInt();
        QVariantList choices;
        for (int level = 0; level <= maximum; ++level)
            choices.append(QVariantMap{{QStringLiteral("value"), level}, {QStringLiteral("label"),
                level == 0 ? qtr("Off") : level == 1 ? qtr("1 (Lowest)")
                : level == maximum ? qtr("%1 (Highest)").arg(level) : QString::number(level)}});
        const auto current = value(name);
        if (current.toInt() == -1)
            choices.append(QVariantMap{{QStringLiteral("value"), -1}, {QStringLiteral("label"), qtr("Custom")}});
        description.insert(QStringLiteral("name"), name);
        description.insert(QStringLiteral("title"), qtr("Video post-processing"));
        description.insert(QStringLiteral("choices"), choices);
        description.insert(QStringLiteral("value"), current);
        description.insert(QStringLiteral("defaultValue"), 0);
        description.insert(QStringLiteral("help"), qtr("Off turns off this filter. A higher level can use more processor time."));
        return description;
    }
    if (name == QLatin1String("redesign-hardware-decoding")) {
        auto description = describe(QStringLiteral("dec-dev"));
        const auto enabled = describe(QStringLiteral("hw-dec"));
        if (description.isEmpty() || enabled.isEmpty()) return {};
        auto choices = description.value(QStringLiteral("choices")).toList();
        for (const auto& entry : {std::pair{QStringLiteral("any"), qtr("Automatic")},
                                  std::pair{QStringLiteral("none"), qtr("Off")}}) {
            auto it = std::find_if(choices.begin(), choices.end(), [&](const auto& row) {
                return row.toMap().value(QStringLiteral("value")).toString() == entry.first;
            });
            const QVariantMap row{{QStringLiteral("value"), entry.first}, {QStringLiteral("label"), entry.second}};
            if (it == choices.end()) choices.append(row);
            else *it = row;
        }
        const auto saved = value(QStringLiteral("dec-dev")).toString();
        if (!saved.isEmpty() && std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == saved;
        })) choices.append(QVariantMap{{QStringLiteral("value"), saved},
            {QStringLiteral("label"), qtr("Saved device (%1)").arg(saved)}});
        const auto deviceDefault = description.value(QStringLiteral("defaultValue")).toString();
        description.insert(QStringLiteral("name"), name);
        description.insert(QStringLiteral("title"), qtr("Hardware-accelerated decoding"));
        description.insert(QStringLiteral("choices"), choices);
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("defaultValue"),
            !enabled.value(QStringLiteral("defaultValue")).toBool() || deviceDefault == QLatin1String("none")
                ? QStringLiteral("none") : deviceDefault.isEmpty() ? QStringLiteral("any") : deviceDefault);
        description.insert(QStringLiteral("help"), qtr("Automatic lets Hakone select a decoder device. Off uses software decoding and keeps your saved device choice. Open the video again after a change."));
        return description;
    }
    if (name == QLatin1String("redesign-subtitle-encoding")) {
        auto description = describe(QStringLiteral("subsdec-encoding"));
        const auto detection = describe(QStringLiteral("subsdec-autodetect-utf8"));
        if (description.isEmpty() || detection.isEmpty()) return {};
        QVariantList choices{
            QVariantMap{{QStringLiteral("label"), qtr("Automatic")}, {QStringLiteral("value"), QStringLiteral("auto:")}}};
        for (const auto& choice : description.value(QStringLiteral("choices")).toList()) {
            auto row = choice.toMap();
            row.insert(QStringLiteral("value"), QString(QStringLiteral("fixed:") + row.value(QStringLiteral("value")).toString()));
            choices.append(row);
        }
        const auto current = value(name).toString();
        const auto encoding = value(QStringLiteral("subsdec-encoding")).toString();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == current;
        })) choices.append(QVariantMap{
            {QStringLiteral("label"), current.startsWith(QLatin1String("auto:"))
                ? qtr("Automatic (fallback: %1)").arg(encoding) : qtr("Custom (%1)").arg(encoding)},
            {QStringLiteral("value"), current}});
        description.insert(QStringLiteral("title"), qtr("Text encoding"));
        description.insert(QStringLiteral("choices"), choices);
        description.insert(QStringLiteral("value"), current);
        description.insert(QStringLiteral("defaultValue"),
            QString((detection.value(QStringLiteral("defaultValue")).toBool() ? QStringLiteral("auto:") : QStringLiteral("fixed:"))
            + description.value(QStringLiteral("defaultValue")).toString()));
        description.insert(QStringLiteral("help"), qtr("Automatic detects UTF-8 text first. If detection fails, Hakone uses the default text encoding. Select a specific encoding to turn off this detection. Open the subtitle file again after a change."));
        if (current.startsWith(QLatin1String("auto:")) && !encoding.isEmpty())
            description.insert(QStringLiteral("help"), QString(description.value(QStringLiteral("help")).toString()
                + QLatin1Char(' ') + qtr("The current automatic fallback is %1.").arg(encoding)));
        return description;
    }
    if (name == QLatin1String("redesign-buffering")) {
        QVariantList choices;
        const int levels[] = {100, 200, 300, 500, 1000, 0};
        const QStringList labels{qtr("Very small"), qtr("Small"), qtr("Normal"), qtr("Large"), qtr("Very large"), qtr("Custom")};
        for (size_t i = 0; i < std::size(levels); ++i)
            choices.append(QVariantMap{{QStringLiteral("label"), labels[i]}, {QStringLiteral("value"), levels[i]}});
        return {{QStringLiteral("kind"), QStringLiteral("integer")}, {QStringLiteral("title"), qtr("Buffering")},
            {QStringLiteral("choices"), choices}, {QStringLiteral("defaultValue"), 300}, {QStringLiteral("value"), value(name)},
            {QStringLiteral("help"), qtr("A larger buffer can reduce pauses, but playback can take longer to start. Normal uses the default buffer values. Select Custom to set each value in milliseconds. New media uses these settings.")}};
    }
    if (name == QLatin1String("redesign-subtitle-background")) {
        auto description = describe(QStringLiteral("freetype-background-mode"));
        if (description.isEmpty()) return {};
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("defaultValue"), 0);
        description.insert(QStringLiteral("choices"), QVariantList{
            QVariantMap{{QStringLiteral("label"), qtr("None")}, {QStringLiteral("value"), 0}},
            QVariantMap{{QStringLiteral("label"), qtr("Box")}, {QStringLiteral("value"), 1}}});
        description.insert(QStringLiteral("help"), qtr("Select None to hide the background. The color and opacity values stay saved."));
        return description;
    }
    if (name == QLatin1String("redesign-subtitle-background-opacity")) {
        auto description = describe(QStringLiteral("freetype-background-opacity"));
        description.insert(QStringLiteral("value"), value(name));
        return description;
    }
    if (name == QLatin1String("redesign-video-zoom")) {
        QVariantList choices{
            QVariantMap{{QStringLiteral("label"), qtr("Fit")}, {QStringLiteral("value"), -1.0}},
            QVariantMap{{QStringLiteral("label"), qtr("Fill window")}, {QStringLiteral("value"), -2.0}},
            QVariantMap{{QStringLiteral("label"), qtr("Fit window width")}, {QStringLiteral("value"), -3.0}},
            QVariantMap{{QStringLiteral("label"), qtr("Fit window height")}, {QStringLiteral("value"), -4.0}},
            QVariantMap{{QStringLiteral("label"), qtr("1:4 Quarter")}, {QStringLiteral("value"), .25}},
            QVariantMap{{QStringLiteral("label"), qtr("1:2 Half")}, {QStringLiteral("value"), .5}},
            QVariantMap{{QStringLiteral("label"), qtr("1:1 Original")}, {QStringLiteral("value"), 1.0}},
            QVariantMap{{QStringLiteral("label"), qtr("2:1 Double")}, {QStringLiteral("value"), 2.0}}
        };
        const auto current = value(name);
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) { return row.toMap().value(QStringLiteral("value")).toDouble() == current.toDouble(); }))
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom (%1×)").arg(current.toDouble())}, {QStringLiteral("value"), current}});
        return {{QStringLiteral("kind"), QStringLiteral("float")}, {QStringLiteral("choices"), choices},
            {QStringLiteral("defaultValue"), -1.0}, {QStringLiteral("value"), current},
            {QStringLiteral("help"), qtr("Fit scales the video to the window. Zoom ratios use the video's actual pixel size.")}};
    }
    if (name == QLatin1String("redesign-snapshot-size")) {
        QVariantList choices{
            QVariantMap{{QStringLiteral("label"), qtr("Same as video")}, {QStringLiteral("value"), QStringLiteral("source")}}
        };
        for (const int width : {640, 1280, 1920})
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("%1 px wide").arg(width)}, {QStringLiteral("value"), QString::number(width)}});
        const auto current = value(name);
        if (current == QLatin1String("custom"))
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom")}, {QStringLiteral("value"), current}});
        return {{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("choices"), choices},
            {QStringLiteral("defaultValue"), QStringLiteral("source")}, {QStringLiteral("value"), current},
            {QStringLiteral("help"), qtr("Set the snapshot width. The height changes to keep the video's aspect ratio. Custom uses your saved width and height.")}};
    }
    if (name == QLatin1String("redesign-reset-picture"))
        return {{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("editor"), QStringLiteral("action")},
            {QStringLiteral("buttonLabel"), qtr("Reset")},
            {QStringLiteral("help"), qtr("Set hue, brightness, contrast, saturation and gamma to their default values.")}};
    if (name == QLatin1String("redesign-audio-passthrough")) {
        const auto option = audioPassthroughOption(audioOutputModule());
        if (option.isEmpty()) return {};
        auto description = describe(option);
        if (description.isEmpty()) return {};
        description.insert(QStringLiteral("name"), name);
        description.insert(QStringLiteral("title"), qtr("Pass compressed audio to the receiver"));
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("help"), qtr("Send compressed audio to the receiver. The receiver must support the audio format. This setting uses the active audio output."));
        if (option == QLatin1String("mmdevice-passthrough")) {
            description.insert(QStringLiteral("kind"), QStringLiteral("bool"));
            description.insert(QStringLiteral("choices"), QVariantList());
            description.insert(QStringLiteral("defaultValue"), description.value(QStringLiteral("defaultValue")).toInt() != 0);
        }
        return description;
    }
    if (name == QLatin1String("redesign-resampling-quality")) {
        QVariantList choices{
            QVariantMap{{QStringLiteral("label"), qtr("Low")}, {QStringLiteral("value"), QStringLiteral("low")}},
            QVariantMap{{QStringLiteral("label"), qtr("Balanced")}, {QStringLiteral("value"), QStringLiteral("balanced")}},
            QVariantMap{{QStringLiteral("label"), qtr("High")}, {QStringLiteral("value"), QStringLiteral("high")}}
        };
        if (value(name) == QStringLiteral("custom"))
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom")}, {QStringLiteral("value"), QStringLiteral("custom")}});
        return {{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("choices"), choices},
            {QStringLiteral("value"), value(name)}, {QStringLiteral("defaultValue"), QStringLiteral("balanced")},
            {QStringLiteral("supported"), resamplingQuality(value(QStringLiteral("audio-resampler")).toString()) != nullptr},
            {QStringLiteral("help"), qtr("Set the quality for audio sample rate conversion. Higher quality uses more processing power. Some resamplers do not have a quality control.")}};
    }
    if (name == QLatin1String("redesign-subtitle-font-size")) {
        const bool pixels = value(QStringLiteral("sub-text-size-mode")).toInt() == 1;
        auto description = describe(pixels ? QStringLiteral("sub-text-size-pixels") : QStringLiteral("sub-text-scale"));
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("editor"), QStringLiteral("number-choice"));
        const QList<int> sizes = pixels ? QList<int>{16, 20, 24, 30, 36, 48, 64} : QList<int>{100, 50, 75, 150, 200};
        const QStringList labels = pixels ? QStringList{} : QStringList{qtr("Automatic"), qtr("Very small"), qtr("Small"), qtr("Large"), qtr("Very large")};
        QVariantList choices;
        for (qsizetype i = 0; i < sizes.size(); ++i)
            choices.append(QVariantMap{{QStringLiteral("value"), sizes[i]},
                {QStringLiteral("label"), pixels ? QString::number(sizes[i]) + QStringLiteral(" px") : labels[i]}});
        const int current = value(name).toInt();
        if (!sizes.contains(current)) {
            const QString label = QString::number(current) + (pixels ? QStringLiteral(" px") : QStringLiteral(" %"));
            choices.append(QVariantMap{{QStringLiteral("value"), current},
                {QStringLiteral("label"), label}});
        }
        description.insert(QStringLiteral("choices"), choices);
        return description;
    }
    if (name == QLatin1String("redesign-subtitle-duration")) {
        if (!module_exists("subsdelay")) return {};
        auto description = describe(QStringLiteral("subsdelay-factor"));
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("defaultValue"), 0.0);
        description.insert(QStringLiteral("help"), qtr("Change how long subtitles stay on screen. Set 0 to use the file duration."));
        return description;
    }
    if (name == QLatin1String("redesign-subtitle-delay")) {
        auto description = describe(QStringLiteral("sub-delay-ms"));
        description.insert(QStringLiteral("value"), value(name));
        description.insert(QStringLiteral("defaultValue"), 0);
        description.insert(QStringLiteral("help"), qtr("Positive values show subtitles later. Negative values show subtitles earlier."));
        return description;
    }
    if (name == QLatin1String("redesign-equalizer-preset")) {
        auto result = describe(QStringLiteral("equalizer-preset"));
        if (result.isEmpty()) return {};
        auto choices = result.value(QStringLiteral("choices")).toList();
        if (value(QStringLiteral("equalizer-preset")).toString().isEmpty()
            && !value(QStringLiteral("equalizer-bands")).toString().isEmpty())
            choices.insert(1, QVariantMap{{QStringLiteral("label"), qtr("Custom")}, {QStringLiteral("value"), QStringLiteral("custom")}, {QStringLiteral("curve"), equalizerBands()}});
        result.insert(QStringLiteral("choices"), choices);
        result.insert(QStringLiteral("value"), value(name));
        result.insert(QStringLiteral("defaultValue"), QString());
        return result;
    }
    if (!trackOption(name).isEmpty())
        return {{QStringLiteral("kind"), QStringLiteral("integer")}, {QStringLiteral("editor"), QStringLiteral("track")},
            {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), INT_MAX},
            {QStringLiteral("defaultValue"), 0}, {QStringLiteral("value"), value(name)},
            {QStringLiteral("help"), qtr("Select a track for the next video. Track numbers start at 1. Automatic uses the language setting.")}};
    if (const auto option = languageOption(name); !option.isEmpty()) {
        const bool menu = option == QLatin1String("menu-language");
        return {{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("editor"), QStringLiteral("language")},
            {QStringLiteral("choices"), languageChoices(value(name).toString(), !menu)},
            {QStringLiteral("defaultValue"), menu ? QString() : QStringLiteral("system")},
            {QStringLiteral("value"), value(name)},
            {QStringLiteral("help"), menu
                ? qtr("Select a language for disc menus. For a list, separate language names or codes with commas. Open the disc again to use this change.")
                : qtr("Select a language. System language uses your system's language. Select Custom to enter a language order. Separate names or codes with commas. This sets the track to Automatic for the next file.")}};
    }
    if (name == QLatin1String("redesign-reset-all"))
        return {{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("editor"), QStringLiteral("action")},
            {QStringLiteral("buttonLabel"), qtr("Reset")}, {QStringLiteral("destructive"), true},
            {QStringLiteral("help"), qtr("Replace all Hakone settings with the default settings.")}};
    if (name == QLatin1String("redesign-config-folder"))
        return {{QStringLiteral("kind"), QStringLiteral("action")}, {QStringLiteral("editor"), QStringLiteral("action")},
            {QStringLiteral("buttonLabel"), qtr("Open")},
            {QStringLiteral("help"), qtr("Open the folder that contains the Hakone settings.")}};
    if (name == QLatin1String("redesign-audio-device")) {
        auto choices = audioDeviceChoices();
        const QString configured = value(name).toString();
        if (!configured.isEmpty() && std::none_of(choices.cbegin(), choices.cend(), [&](const auto& choice) {
            return choice.toMap().value(QStringLiteral("value")).toString() == configured;
        })) choices.append(QVariantMap{{QStringLiteral("label"), qtr("Unavailable device (%1)").arg(configured)}, {QStringLiteral("value"), configured}});
        return {{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("title"), qtr("Output device")},
            {QStringLiteral("choices"), choices}, {QStringLiteral("defaultValue"), QString()}, {QStringLiteral("value"), configured},
            {QStringLiteral("help"), qtr("Select the device to use for audio. If the device is not connected, Hakone uses the audio driver's default device.")}};
    }
    if (name == QLatin1String("redesign-startup-volume"))
        return {{QStringLiteral("kind"), QStringLiteral("integer")}, {QStringLiteral("title"), qtr("Startup volume")},
            {QStringLiteral("minimum"), 0}, {QStringLiteral("maximum"), 300}, {QStringLiteral("defaultValue"), 100},
            {QStringLiteral("value"), value(name)}};

    if (name == QLatin1String("redesign-web-remote"))
        return {{QStringLiteral("kind"), QStringLiteral("bool")}, {QStringLiteral("title"), qtr("Web remote control")},
            {QStringLiteral("defaultValue"), false}, {QStringLiteral("value"), value(name)},
            {QStringLiteral("supported"), supportsWebRemote()},
            {QStringLiteral("help"), supportsWebRemote() ? qtr("Control Hakone from a web browser. After you change the web remote, port or password, close Hakone. Start Hakone again.")
                : qtr("Web remote control is unavailable in this Hakone build.")}};

    if (name == QLatin1String("redesign-repeat") || name == QLatin1String("redesign-after-playback")) {
        const bool repeat = name == QLatin1String("redesign-repeat");
        const QStringList labels = repeat ? QStringList{qtr("Off"), qtr("Current item"), qtr("All items")}
            : QStringList{qtr("Stop"), qtr("Quit Hakone"), qtr("Pause on the last frame")};
        QVariantList choices;
        for (int i = 0; i < labels.size(); ++i) choices.append(QVariantMap{{QStringLiteral("label"), labels[i]}, {QStringLiteral("value"), i}});
        return {{QStringLiteral("kind"), QStringLiteral("integer")}, {QStringLiteral("title"), repeat ? qtr("Repeat") : qtr("When playback finishes")},
            {QStringLiteral("choices"), choices}, {QStringLiteral("defaultValue"), 0}, {QStringLiteral("value"), value(name)}};
    }
    if (name.startsWith(QLatin1String("filter:"))) {
        const auto parts = name.split(QLatin1Char(':'));
        if (parts.size() != 3 || !module_exists(parts[2].toUtf8().constData())) return {};
        return {{QStringLiteral("kind"), QStringLiteral("bool")}, {QStringLiteral("title"), parts[2]},
            {QStringLiteral("value"), value(name)}, {QStringLiteral("defaultValue"), false},
            {QStringLiteral("help"), parts[1] == QLatin1String("audio-filter") ? audioFilterHelp(parts[2]) : QString()}};
    }

    const QByteArray bytes = name.toUtf8();
    const auto* cfg = config_FindConfig(bytes.constData());
    // Removed compatibility options have no UI text. Use the same rule as
    // the native catalog, so curated sections do not expose dead controls.
    if (!cfg || !CONFIG_ITEM(cfg->i_type) || !cfg->psz_text) return {};
    QVariantMap result{{QStringLiteral("name"), name}, {QStringLiteral("title"), qtr(cfg->psz_text ? cfg->psz_text : cfg->psz_name)},
        {QStringLiteral("help"), qtr(cfg->psz_longtext ? cfg->psz_longtext : "")}, {QStringLiteral("value"), value(name)}};
    if (name == QLatin1String("high-priority"))
        result.insert(QStringLiteral("help"), qtr("Request a higher process priority from the system. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("spu"))
        result.insert(QStringLiteral("help"), qtr("Turn this off to hide subtitles. After you turn it on, select a track or open the file again."));
    if (name == QLatin1String("audio"))
        result.insert(QStringLiteral("help"), qtr("Turn this off to stop audio. After you turn it on, select a track or open the file again."));
    if (name == QLatin1String("video"))
        result.insert(QStringLiteral("help"), qtr("Turn this off to hide video. After you turn it on, select a track or open the file again."));
    if (name == QLatin1String("freetype-font") || name == QLatin1String("freetype-monofont")) {
        const bool family = cfg->i_type == CONFIG_ITEM_FONT;
        const bool monospace = name == QLatin1String("freetype-monofont");
        result.insert(QStringLiteral("placeholder"), family
            ? (monospace ? qtr("System monospace") : qtr("System, sans-serif")) : qtr("Font file"));
        if (family) result.insert(QStringLiteral("help"), monospace
            ? qtr("Enter a monospace font family name. Leave the field empty to use the default monospace font.")
            : qtr("Enter a font family name. Leave the field empty to use the default sans-serif font. A subtitle file can specify its own font."));
    }
    if (name == QLatin1String("freetype-text-direction"))
        result.insert(QStringLiteral("help"), qtr("Automatic detects the paragraph direction from the text. Select a direction if the text is not in the correct order."));
    if (name == QLatin1String("input-record-path")) {
        result.insert(QStringLiteral("placeholder"), qtr("Automatic"));
        result.insert(QStringLiteral("help"), qtr("Leave the field empty to use your Videos folder for video recordings and your Music folder for audio recordings. Direct stream recordings use your Downloads folder."));
    }
#ifdef _WIN32
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
    if (name == QLatin1String("http-proxy"))
        result.insert(QStringLiteral("placeholder"), qtr("System proxy"));
#endif
#endif
    if (name == QLatin1String("aout"))
        result.insert(QStringLiteral("help"), qtr("Select an audio output driver. Close Hakone. Start Hakone again to use a new driver."));
    if (name == QLatin1String("pitch-shift"))
        result.insert(QStringLiteral("help"), qtr("Set the pitch in semitones. Use 0 to turn off this effect. Playback speed does not change."));
    if (name == QLatin1String("audio-spatial") || name == QLatin1String("audio-spatial-mode")) {
        const bool supported = module_exists("spatialaudio");
        result.insert(QStringLiteral("supported"), supported);
        if (!supported) result.insert(QStringLiteral("help"), qtr("Hakone cannot use spatial audio in this version."));
    }
    QVariantList choices;
    QString kind;
    switch (CONFIG_CLASS(cfg->i_type)) {
    case CONFIG_ITEM_BOOL:
        kind = QStringLiteral("bool"); result.insert(QStringLiteral("defaultValue"), bool(cfg->orig.i)); break;
    case CONFIG_ITEM_INTEGER: {
        kind = QStringLiteral("integer");
        result.insert(QStringLiteral("defaultValue"), QVariant::fromValue<qlonglong>(cfg->orig.i));
        result.insert(QStringLiteral("minimum"), QVariant::fromValue<qlonglong>(cfg->min.i));
        result.insert(QStringLiteral("maximum"), QVariant::fromValue<qlonglong>(cfg->max.i));
        int64_t* values = nullptr; char** labels = nullptr;
        const ssize_t count = config_GetIntChoices(bytes.constData(), &values, &labels);
        for (ssize_t i = 0; i < count; ++i) {
            choices.append(QVariantMap{{QStringLiteral("label"), configChoiceText(cfg, i, nullptr, labels[i])}, {QStringLiteral("value"), QVariant::fromValue<qlonglong>(values[i])}});
            free(labels[i]);
        }
        free(values); free(labels);
        break;
    }
    case CONFIG_ITEM_FLOAT:
        kind = QStringLiteral("float"); result.insert(QStringLiteral("defaultValue"), cfg->orig.f);
        result.insert(QStringLiteral("minimum"), cfg->min.f); result.insert(QStringLiteral("maximum"), cfg->max.f); break;
    case CONFIG_ITEM_STRING: {
        kind = cfg->i_type == CONFIG_ITEM_PASSWORD ? QStringLiteral("password") : QStringLiteral("string");
        result.insert(QStringLiteral("defaultValue"), qfu(cfg->orig.psz ? cfg->orig.psz : ""));
        char** values = nullptr; char** labels = nullptr;
        const ssize_t count = config_GetPszChoices(bytes.constData(), &values, &labels);
        for (ssize_t i = 0; i < count; ++i) {
            choices.append(QVariantMap{{QStringLiteral("label"), configChoiceText(cfg, i, values[i], labels[i])}, {QStringLiteral("value"), qfu(values[i])}});
            free(values[i]); free(labels[i]);
        }
        free(values); free(labels);
        break;
    }
    }
    QString editor;
    switch (cfg->i_type) {
    case CONFIG_ITEM_LOADFILE: case CONFIG_ITEM_SAVEFILE: editor = QStringLiteral("file"); break;
    case CONFIG_ITEM_DIRECTORY: editor = QStringLiteral("directory"); break;
    case CONFIG_ITEM_RGB: editor = QStringLiteral("color"); break;
    }
    if (name == QLatin1String("qt-fullscreen-screennumber")) {
        choices.append(QVariantMap{{QStringLiteral("label"), qtr("Current screen")}, {QStringLiteral("value"), -1}});
        const auto screens = QGuiApplication::screens();
        for (int i = 0; i < screens.size(); ++i)
            choices.append(QVariantMap{{QStringLiteral("label"), screens[i]->name()}, {QStringLiteral("value"), i}});
    }
    if (name == QLatin1String("input-timeshift-granularity")) {
        choices = {
            QVariantMap{{QStringLiteral("label"), qtr("Small")}, {QStringLiteral("value"), 10 * 1024 * 1024}},
            QVariantMap{{QStringLiteral("label"), qtr("Medium")}, {QStringLiteral("value"), -1}},
            QVariantMap{{QStringLiteral("label"), qtr("Large")}, {QStringLiteral("value"), 100 * 1024 * 1024}}
        };
        const auto current = value(name).toLongLong();
        if (current != -1 && current != 10 * 1024 * 1024 && current != 100 * 1024 * 1024)
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom (%1 MiB)").arg(current / (1024. * 1024.), 0, 'g', 4)}, {QStringLiteral("value"), current}});
        result.insert(QStringLiteral("help"), qtr("Set the maximum size of each temporary file used for time-shift playback. Small uses 10 MiB, Medium uses 50 MiB and Large uses 100 MiB."));
    }
    if (name == QLatin1String("sub-autodetect-fuzzy")) {
        choices = {
            QVariantMap{{QStringLiteral("label"), qtr("Off")}, {QStringLiteral("value"), 0}},
            QVariantMap{{QStringLiteral("label"), qtr("Any title")}, {QStringLiteral("value"), 1}},
            QVariantMap{{QStringLiteral("label"), qtr("Contains title")}, {QStringLiteral("value"), 2}},
            QVariantMap{{QStringLiteral("label"), qtr("Title with extra characters")}, {QStringLiteral("value"), 3}},
            QVariantMap{{QStringLiteral("label"), qtr("Exact title match")}, {QStringLiteral("value"), 4}}
        };
        const auto current = value(name).toLongLong();
        if (current < 0 || current > 4)
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom (%1)").arg(current)}, {QStringLiteral("value"), current}});
        result.insert(QStringLiteral("help"), qtr("Select how closely subtitle file names must match the video title."));
    }
    if (name == QLatin1String("freetype-shadow-distance")) {
        choices = {
            QVariantMap{{QStringLiteral("label"), qtr("Small")}, {QStringLiteral("value"), .06f}},
            QVariantMap{{QStringLiteral("label"), qtr("Medium")}, {QStringLiteral("value"), .12f}},
            QVariantMap{{QStringLiteral("label"), qtr("Large")}, {QStringLiteral("value"), .24f}}
        };
        const float current = value(name).toFloat();
        if (current != .06f && current != .12f && current != .24f)
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Custom (%1)").arg(current)}, {QStringLiteral("value"), current}});
        result.insert(QStringLiteral("help"), qtr("Set the distance between the text and its shadow."));
    }
    if (name == QLatin1String("sout-x264-tune")) {
        choices.prepend(QVariantMap{{QStringLiteral("label"), qtr("None")}, {QStringLiteral("value"), QString()}});
        result.insert(QStringLiteral("help"), qtr("Select settings for the type of video. None uses the default settings."));
    }
    if (name == QLatin1String("sout-x264-preset"))
        result.insert(QStringLiteral("help"), qtr("Select the encoding speed. Slow presets use more processor time and can produce smaller files."));
    if (name == QLatin1String("aspect-ratio") || name == QLatin1String("crop")) {
        const bool crop = name == QLatin1String("crop");
        choices = {QVariantMap{{QStringLiteral("label"), crop ? qtr("None") : qtr("Automatic")}, {QStringLiteral("value"), QString()}}};
        QStringList ratios = crop ? QStringList{QStringLiteral("16:10"), QStringLiteral("16:9"), QStringLiteral("1.85:1"), QStringLiteral("2.21:1"), QStringLiteral("2.35:1"), QStringLiteral("2.39:1"), QStringLiteral("5:3"), QStringLiteral("4:3"), QStringLiteral("5:4"), QStringLiteral("1:1")}
            : QStringList{QStringLiteral("1:1"), QStringLiteral("4:3"), QStringLiteral("5:4"), QStringLiteral("16:9"), QStringLiteral("16:10"), QStringLiteral("2.21:1"), QStringLiteral("2.35:1"), QStringLiteral("2.39:1")};
        ratios += value(crop ? QStringLiteral("custom-crop-ratios") : QStringLiteral("custom-aspect-ratios")).toString().split(QLatin1Char(','));
        ratios.append(value(name).toString());
        QSet<QString> seen{QString()};
        for (const auto& text : ratios) {
            const auto ratio = text.trimmed();
            if (seen.contains(ratio)) continue;
            seen.insert(ratio);
            choices.append(QVariantMap{{QStringLiteral("label"), ratio}, {QStringLiteral("value"), ratio}});
        }
    }
    if (name == QLatin1String("equalizer-preset")) {
        choices.clear();
        choices.append(QVariantMap{{QStringLiteral("label"), qtr("None")}, {QStringLiteral("value"), QString()}});
        for (int i = 0; i < NB_PRESETS; ++i) {
            QVariantList curve;
            for (int band = 0; band < eqz_preset_10b[i].i_band; ++band)
                curve.append(eqz_preset_10b[i].f_amp[band]);
            choices.append(QVariantMap{{QStringLiteral("label"), qtr(preset_list_text[i])},
                {QStringLiteral("value"), qfu(preset_list[i])}, {QStringLiteral("curve"), curve}});
        }
    }
    result.insert(QStringLiteral("editor"), editor);
    if (name == QLatin1String("file-logging"))
        result.insert(QStringLiteral("help"), qtr("Save messages to a file. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("logfile"))
        result.insert(QStringLiteral("help"), qtr("Select the log file. Hakone adds new messages to the end of this file. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("logmode"))
        result.insert(QStringLiteral("help"), qtr("Select plain text or HTML for the log file. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("verbose"))
        result.insert(QStringLiteral("help"), qtr("Select the Hakone log level. File logs can use this level or a separate file log level. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("qt-system-tray"))
        result.insert(QStringLiteral("help"), qtr("Show the Hakone icon in the system tray."));
    if (name == QLatin1String("qt-start-minimized"))
        result.insert(QStringLiteral("help"), qtr("Start Hakone in the system tray. This turns on the tray icon. After a change, close Hakone. Start Hakone again."));
    if (name == QLatin1String("qt-notification"))
        result.insert(QStringLiteral("help"), qtr("Show a track message always, only when Hakone is hidden or minimized, or never."));
    if (name == QLatin1String("qt-notification-timeout"))
        result.insert(QStringLiteral("help"), qtr("Select how long Hakone shows the track popup. The system can change this time."));
    if (name == QLatin1String("qt-opacity"))
        result.insert(QStringLiteral("help"), qtr("Change the opacity of the main Hakone window. The change applies at once."));
    if (name == QLatin1String("qt-fs-opacity"))
        result.insert(QStringLiteral("help"), qtr("Change the opacity of the control backgrounds in fullscreen. The text and icons stay clear."));
    if (name == QLatin1String("qt-fs-sensitivity"))
        result.insert(QStringLiteral("help"), qtr("Minimum pointer movement in pixels before fullscreen controls appear near an edge. A smaller value increases sensitivity."));
    if (name == QLatin1String("qt-embedded-open"))
        result.insert(QStringLiteral("help"), qtr("Show a file browser in Open Media. When off, use Add to select files. Changes apply when you open the dialog again."));
    if (name == QLatin1String("qt-video-autoresize"))
        result.insert(QStringLiteral("help"), qtr("Select Never to keep the window size. Select Always to resize the window when a video opens. Fullscreen and maximized windows keep their size. Audio visualizers do not change the window size."));
    if (name == QLatin1String("qt-minimal-view"))
        result.insert(QStringLiteral("help"), qtr("Start Hakone in compact mode. After a change, close Hakone. Start Hakone again. Launch options can override this setting."));
    if (name == QLatin1String("qt-verbose"))
        result.insert(QStringLiteral("help"), qtr("Add internal interface information, warnings and errors to the Hakone log. Use Log details to select which messages to save."));
    if (name == QLatin1String("qt-error-dialogs"))
        result.insert(QStringLiteral("help"), qtr("Show error messages and confirmation questions. When this is off, Hakone cancels normal and warning questions. Errors stay in the error history. Login, progress and critical questions stay on."));
    if (name == QLatin1String("language")) {
        choices = installedInterfaceLanguages();
        choices.prepend(QVariantMap{{QStringLiteral("label"), qtr("System language")}, {QStringLiteral("value"), QStringLiteral("auto")}});
        const auto current = value(name).toString();
        if (!installedInterfaceLanguage(current))
            choices.append(QVariantMap{{QStringLiteral("label"), qtr("Saved language (%1)").arg(current)}, {QStringLiteral("value"), current}});
        result.insert(QStringLiteral("editor"), QStringLiteral("choice"));
        result.insert(QStringLiteral("defaultValue"), QStringLiteral("auto"));
        result.insert(QStringLiteral("help"), qtr("Select a language for the interface. Changes apply at once. If text has no translation, Hakone shows it in English."));
    }
    if (name == QLatin1String("codec") || name == QLatin1String("sout-transcode-venc")
        || name == QLatin1String("sout-transcode-aenc") || name == QLatin1String("sout-transcode-senc")) {
        const bool decoder = name == QLatin1String("codec");
        const auto capabilities = decoder
            ? QStringList{QStringLiteral("video decoder"), QStringLiteral("audio decoder"), QStringLiteral("spu decoder")}
            : QStringList{name == QLatin1String("sout-transcode-venc") ? QStringLiteral("video encoder")
                : name == QLatin1String("sout-transcode-aenc") ? QStringLiteral("audio encoder") : QStringLiteral("spu encoder")};
        choices = moduleChoices(capabilities);
        const auto current = value(name).toString();
        const auto automatic = decoder || current == QLatin1String("any") ? QStringLiteral("any") : QStringLiteral("none");
        choices.prepend(QVariantMap{{QStringLiteral("label"), qtr("Automatic")}, {QStringLiteral("value"), automatic}});
        if (!current.isEmpty() && std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == current;
        })) choices.append(QVariantMap{{QStringLiteral("label"), decoder ? qtr("Saved list (%1)").arg(current)
            : qtr("Saved encoder (%1)").arg(current)}, {QStringLiteral("value"), current}});
        result.insert(QStringLiteral("help"), decoder
            ? qtr("Select the decoder to try first. Automatic lets Hakone select it. Open the file again after a change.")
            : qtr("Hakone uses this encoder for conversion and streaming. It must support the output format. An output profile can override this choice."));
    }
    if (name == QLatin1String("startup-options"))
        result.insert(QStringLiteral("help"), qtr("Enter Hakone options only. Use quotes for text with spaces. The launch command takes priority. After a change, close Hakone. Start Hakone again. Use --ignore-config to start without saved options."));
    if (name == QLatin1String("clock-synchro"))
        result.insert(QStringLiteral("help"), qtr("Control input clock synchronization for live streams. Default uses the source settings. Open the stream again after a change."));
    if (name == QLatin1String("clock-jitter"))
        result.insert(QStringLiteral("help"), qtr("Set the maximum variation in input delay that Hakone can correct, in milliseconds. Open the file or stream again after a change."));
    if (name == QLatin1String("cr-average"))
        result.insert(QStringLiteral("help"), qtr("Set the number of clock samples used to smooth timing changes. Higher values give a slower response. The input buffer also affects this value. Open the file or stream again after a change."));
    if (name == QLatin1String("media-library-startup")) {
        const bool supported = module_exists("medialibrary");
        result.insert(QStringLiteral("supported"), supported);
        result.insert(QStringLiteral("restartRequired"), supported && m_ctx && m_libraryStartupEdited
            && value(name).toBool() != m_ctx->hasMediaLibrary());
        if (!supported)
            result.insert(QStringLiteral("help"), qtr("The media library is not available in this build."));
    }
    if (name == QLatin1String("recursive")) {
        const bool supported = !m_ctx || directoryDepth(m_ctx) != VLC_PLAYLIST_PREPARSING_DISABLED;
        result.insert(QStringLiteral("supported"), supported);
        result.insert(QStringLiteral("help"), supported
            ? qtr("Include subfolders adds files from the folder and keeps its subfolders in the queue. Open folders on playback reads a folder when you play it. Expand all subfolders reads all files at once. Changes apply to the next folder you add.")
            : value(QStringLiteral("auto-preparse")).toBool()
                ? qtr("Close Hakone. Start Hakone again to use this option. A command-line option can stop Hakone from reading file information.")
                : qtr("Turn on \"Read file metadata automatically\" in Advanced Options. Close Hakone. Start Hakone again to use this option."));
    }
    if (name == QLatin1String("auto-preparse")) {
        result.insert(QStringLiteral("restartRequired"), m_ctx && m_autoPreparseEdited
            && value(name).toBool() != (directoryDepth(m_ctx) != VLC_PLAYLIST_PREPARSING_DISABLED));
        result.insert(QStringLiteral("help"), qtr("Read file information for items added to the queue. After a change, close Hakone. Start Hakone again."));
    }
    if (name == QLatin1String("one-instance")) {
        auto* instance = m_ctx ? vlc_object_instance(m_ctx->getIntf()) : nullptr;
        const bool listening = instance && var_Type(instance, "single-instance-listening") != 0
            && var_GetBool(instance, "single-instance-listening");
        result.insert(QStringLiteral("restartRequired"), m_ctx && m_singleInstanceEdited
            && value(name).toBool() && !listening);
        result.insert(QStringLiteral("help"), qtr("Use one Hakone window for app launches. The file manager option also includes files that you open from the file manager. After you turn this on, close all Hakone windows. Start Hakone again. Command-line options can override these settings."));
    }
    if (name == QLatin1String("one-instance-when-started-from-file"))
        result.insert(QStringLiteral("help"), qtr("Use the same Hakone window for files opened from the file manager."));
    if (name == QLatin1String("playlist-enqueue"))
        result.insert(QStringLiteral("help"), qtr("Keep the current item playing and add new files to the queue. Otherwise, start the new files."));
    if (name == QLatin1String("dvd")) {
        QStringList devices;
#ifdef _WIN32
        for (const auto& drive : QDir::drives()) {
            const auto path = drive.absoluteFilePath();
            if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(path.utf16())) == DRIVE_CDROM)
                devices.append(QDir::toNativeSeparators(path));
        }
#else
        for (const auto& device : QDir(QStringLiteral("/dev")).entryList(
                 {QStringLiteral("dvd*"), QStringLiteral("cdrom*"), QStringLiteral("scd*"), QStringLiteral("sr*")}, QDir::System))
            devices.append(QStringLiteral("/dev/") + device);
        for (const auto& volume : QStorageInfo::mountedVolumes()) {
            const auto type = volume.fileSystemType().toLower();
            if (type == "udf" || type == "iso9660" || type == "cd9660")
                devices.append(QString::fromLocal8Bit(volume.device()));
        }
#endif
        devices.append(value(name).toString());
        devices.removeAll(QString()); devices.removeDuplicates();
        choices = {QVariantMap{{QStringLiteral("label"), qtr("System default")}, {QStringLiteral("value"), QString()}}};
        for (const auto& path : devices)
            choices.append(QVariantMap{{QStringLiteral("label"), path}, {QStringLiteral("value"), path}});
        result.insert(QStringLiteral("editor"), QStringLiteral("optical-drive"));
        result.insert(QStringLiteral("help"), qtr("Select a drive or enter its path. Open the disc again to use this change."));
    }
    result.insert(QStringLiteral("kind"), kind);
    result.insert(QStringLiteral("choices"), choices);
    return result;
}

bool RedesignPreferences::stage(const QString& name, const QVariant& proposed)
{
    if (name == QLatin1String("redesign-fullscreen-sensitivity")) {
        bool valid = false;
        const double level = proposed.toDouble(&valid);
        if (!valid || !std::isfinite(level) || level != std::floor(level) || level < 0 || level >= std::size(fullscreenSensitivityLevels)) return false;
        return stage(QStringLiteral("qt-fs-sensitivity"), fullscreenSensitivityLevels[int(level)]);
    }
    if (name == QLatin1String("qt-start-minimized") && proposed.toBool()
        && !value(QStringLiteral("qt-system-tray")).toBool()) {
        if (!stage(QStringLiteral("qt-system-tray"), true)) return false;
    }
    if (name == QLatin1String("qt-system-tray") && !proposed.toBool()
        && value(QStringLiteral("qt-start-minimized")).toBool()) {
        if (!stage(QStringLiteral("qt-start-minimized"), false)) return false;
    }
    if (name == QLatin1String("redesign-log-details")) {
        if (proposed.metaType().id() != QMetaType::QString) return false;
        const auto selection = proposed.toString();
        if (selection.isEmpty()) return false;
        const auto choices = describe(QStringLiteral("log-level")).value(QStringLiteral("choices")).toList();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& choice) {
            return choice.toMap().value(QStringLiteral("value")).toString() == selection;
        })) return selection == value(name).toString();
        return stage(QStringLiteral("log-level"), selection);
    }
    if (name == QLatin1String("file-logging") && proposed.toBool() && !value(name).toBool()
        && value(QStringLiteral("log-level")).toString().isEmpty() && config_FindConfig("log-level")) {
        // The untouched design choice is Errors only. Apply it when a user
        // first enables file logging; native callers can still inherit verbosity.
        if (!stage(QStringLiteral("log-level"), QStringLiteral("errors"))) return false;
    }
    if (name == QLatin1String("redesign-postprocessing")) {
        const auto *cfg = config_FindConfig("postproc-q");
        bool valid = false;
        const int level = proposed.toInt(&valid);
        if (!cfg || !valid || proposed.metaType().id() == QMetaType::Bool
            || proposed.toDouble() != level) return false;
        if (level == -1) return value(name).toInt() == -1;
        if (level < 0 || level > cfg->max.i) return false;
        const auto chain = vlc::postprocessing::withQuality(videoValue(QStringLiteral("video-filter")).toString(), level);
        // Inline q values override the inherited level. Update both, then set
        // the chain so existing and new filters receive the selected quality.
        return (level == 0 || stage(QStringLiteral("postproc-q"), level))
            && stage(QStringLiteral("video-filter"), chain);
    }
    if (name == QLatin1String("redesign-hardware-decoding")) {
        if (proposed.metaType().id() != QMetaType::QString) return false;
        const auto selection = proposed.toString();
        const auto choices = describe(name).value(QStringLiteral("choices")).toList();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == selection;
        })) return false;
        if (selection == QLatin1String("none"))
            return stage(QStringLiteral("hw-dec"), false);
        return stage(QStringLiteral("dec-dev"), selection) && stage(QStringLiteral("hw-dec"), true);
    }
    if (name == QLatin1String("redesign-subtitle-encoding")) {
        if (proposed.metaType().id() != QMetaType::QString) return false;
        const auto selection = proposed.toString();
        const auto choices = describe(name).value(QStringLiteral("choices")).toList();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& row) {
            return row.toMap().value(QStringLiteral("value")).toString() == selection;
        })) return false;
        if (selection == value(name).toString()) return true;
        const bool automatic = selection.startsWith(QLatin1String("auto:"));
        const auto encoding = selection.mid(automatic ? 5 : 6);
        return stage(QStringLiteral("subsdec-encoding"), encoding)
            && stage(QStringLiteral("subsdec-autodetect-utf8"), automatic);
    }
    if (name == QLatin1String("redesign-buffering")) {
        bool valid = false;
        const int level = proposed.toInt(&valid);
        if (!valid || proposed.toDouble() != level
            || (level != 0 && std::find(std::begin(bufferingLevels), std::end(bufferingLevels), level) == std::end(bufferingLevels))) return false;
        if (level != 0)
            for (const char *option : bufferingOptions)
                if (!stage(qfu(option), bufferingValue(option, level))) return false;
        return stage(QStringLiteral("qt-buffering-custom"), level == 0);
    }
    if (name == QLatin1String("redesign-video-zoom")) {
        bool valid = false;
        const auto zoom = proposed.toDouble(&valid);
        if (!valid || !std::isfinite(zoom) || (zoom <= 0 && (zoom > -1 || zoom < -4 || std::trunc(zoom) != zoom))) return false;
        if (zoom < 0)
            return stage(QStringLiteral("fit"), -zoom) && stage(QStringLiteral("autoscale"), true);
        return stage(QStringLiteral("zoom"), zoom) && stage(QStringLiteral("fit"), 0) && stage(QStringLiteral("autoscale"), false);
    }
    if (name == QLatin1String("redesign-snapshot-size")) {
        const auto size = proposed.toString();
        if (size == QLatin1String("custom")) return value(name) == size;
        if (size != QLatin1String("source") && size != QLatin1String("640")
            && size != QLatin1String("1280") && size != QLatin1String("1920")) return false;
        return stage(QStringLiteral("snapshot-width"), size == QLatin1String("source") ? -1 : size.toInt())
            && stage(QStringLiteral("snapshot-height"), size == QLatin1String("source") ? -1 : 0);
    }
    if (name == QLatin1String("redesign-audio-passthrough")) {
        const auto option = audioPassthroughOption(audioOutputModule());
        if (option.isEmpty()) return false;
        if (option == QLatin1String("mmdevice-passthrough")) {
            if (proposed.metaType().id() != QMetaType::Bool) return false;
            // Preserve an existing AC3/DTS-only policy; enable all supported
            // compressed formats when switching on from Disabled.
            const int current = value(option).toInt();
            const int enabled = current > 0 ? current : 2;
            return stage(option, proposed.toBool() ? enabled : 0);
        }
        return stage(option, proposed);
    }
    if (name == QLatin1String("redesign-resampling-quality")) {
        const auto* quality = resamplingQuality(value(QStringLiteral("audio-resampler")).toString());
        if (!quality) return false;
        const auto level = proposed.toString();
        if (level == QLatin1String("custom")) return value(name) == proposed;
        if (level != QLatin1String("low") && level != QLatin1String("balanced") && level != QLatin1String("high")) return false;
        const int setting = level == QLatin1String("low") ? quality->low
            : level == QLatin1String("high") ? quality->high : quality->balanced;
        if (!stage(qfu(quality->option), setting)) return false;
        // Select the matching converter when Automatic would otherwise fall
        // back to a resampler that has no quality control.
        return stage(QStringLiteral("audio-resampler"), qfu(quality->module));
    }
    if (name == QLatin1String("redesign-subtitle-background")) {
        bool valid = false;
        const auto mode = proposed.toLongLong(&valid);
        if (!valid || (mode != 0 && mode != 1)) return false;
        if (mode == 1 && value(QStringLiteral("freetype-background-opacity")).toInt() == 0
            && !stage(QStringLiteral("freetype-background-opacity"), 128)) return false;
        return stage(QStringLiteral("freetype-background-mode"), mode);
    }
    if (name == QLatin1String("redesign-subtitle-background-opacity")) {
        bool valid = false;
        const auto opacity = proposed.toLongLong(&valid);
        if (!valid || opacity < 0 || opacity > 255) return false;
        // Editing opacity in Settings retains the selected background mode.
        // Resolve the legacy automatic mode before changing its opacity.
        return stage(QStringLiteral("freetype-background-mode"), value(QStringLiteral("redesign-subtitle-background")))
            && stage(QStringLiteral("freetype-background-opacity"), opacity);
    }
    if (name == QLatin1String("redesign-subtitle-font-size"))
        return stage(value(QStringLiteral("sub-text-size-mode")).toInt() == 1
            ? QStringLiteral("sub-text-size-pixels") : QStringLiteral("sub-text-scale"), proposed);
    if (name == QLatin1String("redesign-subtitle-duration")) {
        if (!module_exists("subsdelay") || !stage(QStringLiteral("subsdelay-factor"), proposed)) return false;
        if (proposed.toDouble() > 0) stage(QStringLiteral("subsdelay-mode"), 1);
        return stage(QStringLiteral("filter:sub-filter:subsdelay"), proposed.toDouble() > 0);
    }
    if (name == QLatin1String("redesign-subtitle-delay")) {
        if (!stage(QStringLiteral("sub-delay-ms"), proposed)) return false;
        return stage(QStringLiteral("sub-delay"), 0);
    }
    if (name == QLatin1String("redesign-equalizer-preset")) {
        const auto preset = proposed.toString();
        if (preset.isEmpty()) return stage(QStringLiteral("filter:audio-filter:equalizer"), false);
        if (preset == QLatin1String("custom")) {
            if (!value(QStringLiteral("equalizer-preset")).toString().isEmpty()) return false;
        } else if (!stage(QStringLiteral("equalizer-preset"), preset)) return false;
        return stage(QStringLiteral("filter:audio-filter:equalizer"), true);
    }
    if (const auto option = trackOption(name); !option.isEmpty()) {
        bool valid = false;
        const auto number = proposed.toLongLong(&valid);
        if (!stage(option, valid && number >= 0 && number <= INT_MAX ? QVariant(number - 1) : QVariant(QStringLiteral("invalid")))) return false;
        // A legacy stream ID would otherwise override the chosen number.
        return stage(option + QStringLiteral("-id"), QString());
    }
    if (const auto option = languageOption(name); !option.isEmpty()) {
        const QString input = proposed.toString().trimmed();
        if (input == value(name).toString()) {
            m_invalid.remove(name);
            if (!stage(option, value(option))) return false;
            if (option == QLatin1String("menu-language")) return true;
            const QString track = option == QLatin1String("audio-language") ? QStringLiteral("audio-track") : QStringLiteral("sub-track");
            stage(track, -1);
            return stage(track + QStringLiteral("-id"), QString());
        }
        QStringList codes;
        bool valid = true;
        if (!input.isEmpty() && input.compare(qtr("Automatic"), Qt::CaseInsensitive) != 0) {
            for (const auto& token : input.split(QLatin1Char(','))) {
                const auto code = token.trimmed().toLower().toUtf8();
                const auto* language = code.isEmpty() ? nullptr : languageFromText(token.trimmed());
                if (option != QLatin1String("menu-language")
                    && (token.trimmed().compare(qtr("System language"),Qt::CaseInsensitive)==0 || code=="system")) codes.append(QStringLiteral("system"));
                else if (language) codes.append(qfu(*language->psz_iso639_1 ? language->psz_iso639_1 : language->psz_iso639_2T));
                else if (code == "any" || (option != QLatin1String("menu-language") && code == "none")) codes.append(qfu(code.constData()));
                else { valid = false; break; }
            }
        }
        if (!valid) {
            m_invalid.insert(name);
            m_error = qtr("Enter a language name or a two or three letter language code.");
            ++m_revision; emit changed(); return false;
        }
        m_invalid.remove(name);
        if (!stage(option, codes.join(QLatin1Char(',')))) return false;
        if (option == QLatin1String("menu-language")) return true;
        const QString track = option == QLatin1String("audio-language") ? QStringLiteral("audio-track") : QStringLiteral("sub-track");
        stage(track, -1);
        return stage(track + QStringLiteral("-id"), QString());
    }
    if (name == QLatin1String("audio-spatial") || name == QLatin1String("audio-spatial-mode")) {
        if (!module_exists("spatialaudio")) return false;
        if (name == QLatin1String("audio-spatial-mode") && proposed.toInt() != AOUT_MIX_MODE_BINAURAL
            && proposed.toInt() != AOUT_MIX_MODE_UNSET) return false;
    }
    if (name == QLatin1String("redesign-startup-volume")) {
        bool valid = false;
        const auto volume = proposed.toLongLong(&valid);
        return stage(QStringLiteral("audio-startup-volume"), valid && volume >= 0 && volume <= 300 ? QVariant(volume) : QVariant(QStringLiteral("invalid")));
    }
    if (name == QLatin1String("redesign-audio-device")) {
        const QString id = proposed.toString();
        const auto choices = audioDeviceChoices();
        if (std::none_of(choices.cbegin(), choices.cend(), [&](const auto& choice) { return choice.toMap().value(QStringLiteral("value")).toString() == id; })) return false;
        const QString module = audioOutputModule();
        stage(QStringLiteral("audio-device-default-module"), id.isEmpty() ? QString() : module);
        const QString legacyName = module + QStringLiteral("-audio-device");
        const auto* legacy = config_FindConfig(legacyName.toUtf8().constData());
        if (legacy && CONFIG_CLASS(legacy->i_type) == CONFIG_ITEM_STRING)
            stage(legacyName, id.isEmpty() ? qfu(legacy->orig.psz ? legacy->orig.psz : "") : id);
        return stage(QStringLiteral("audio-device-default"), id);
    }
    if (name == QLatin1String("volume-save")) {
        if (proposed.toBool() && m_invalid.contains(QStringLiteral("audio-startup-volume")))
            stage(QStringLiteral("audio-startup-volume"), value(QStringLiteral("audio-startup-volume")));
        if (!proposed.toBool() && value(QStringLiteral("audio-startup-volume")).toInt() < 0)
            stage(QStringLiteral("audio-startup-volume"), value(QStringLiteral("redesign-startup-volume")));
        if (proposed.toBool() && m_ctx) {
            const float current = vlc_player_aout_GetVolume(m_ctx->getIntf()->p_player);
            if (std::isfinite(current) && current >= 0) stage(QStringLiteral("audio-last-volume"), std::min(current, 3.f));
        }
    }

    if (name == QLatin1String("redesign-web-remote")) {
        if (proposed.toBool() && !supportsWebRemote()) return false;
        auto interfaces = value(QStringLiteral("extraintf")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
        interfaces.erase(std::remove_if(interfaces.begin(), interfaces.end(), [](const auto& entry) {
            const auto module = entry.section(QLatin1Char('{'), 0, 0);
            return module == QLatin1String("http") || module == QLatin1String("luahttp");
        }), interfaces.end());
        if (proposed.toBool()) interfaces.append(QStringLiteral("http"));
        return stage(QStringLiteral("extraintf"), interfaces.join(QLatin1Char(':')));
    }

    if (name == QLatin1String("redesign-repeat") || name == QLatin1String("redesign-after-playback")) {
        const int choice = proposed.toInt();
        if (choice < 0 || choice > 2) return false;
        const bool repeat = name == QLatin1String("redesign-repeat");
        stage(repeat ? QStringLiteral("repeat") : QStringLiteral("play-and-exit"), choice == 1);
        return stage(repeat ? QStringLiteral("loop") : QStringLiteral("play-and-pause"), choice == 2);
    }
    if (name.startsWith(QLatin1String("filter:"))) {
        const auto parts = name.split(QLatin1Char(':'));
        if (parts.size() != 3 || !module_exists(parts[2].toUtf8().constData())) return false;
        auto filters = (parts[1] == QLatin1String("video-filter") || parts[1] == QLatin1String("sub-source")
            ? videoValue(parts[1]) : value(parts[1])).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
        // Older configurations can put sources in sub-filter. Move the
        // active sources together, so enabling one does not hide another.
        if (parts[1] == QLatin1String("sub-source")) {
            const auto legacy = videoValue(QStringLiteral("sub-filter")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
            QStringList retained;
            for (const auto& entry : legacy) {
                const auto* module = module_find(entry.toUtf8().constData());
                if (module && module_provides(module, "sub source")) {
                    if (!filters.contains(entry)) filters.append(entry);
                } else {
                    retained.append(entry);
                }
            }
            if (retained != legacy)
                stage(QStringLiteral("sub-filter"), retained.join(QLatin1Char(':')));
        }
        // Editing an enabled effect must preserve its position. Moving an
        // adjust filter after transform can discard the transform orientation.
        if (!proposed.toBool()) filters.removeAll(parts[2]);
        else if (!filters.contains(parts[2])) {
            // Transform describes the final display orientation. A new pixel
            // effect after it can replace that format and lose the flip.
            const int transform = filters.indexOf(QStringLiteral("transform"));
            if (parts[1] == QLatin1String("video-filter") && transform >= 0)
                filters.insert(transform, parts[2]);
            else
                filters.append(parts[2]);
        }
        return stage(parts[1], filters.join(QLatin1Char(':')));
    }

    if (name == QLatin1String("equalizer-preset") && !proposed.toString().isEmpty()) {
        const QByteArray preset = proposed.toString().toUtf8();
        const auto it = std::find_if(std::begin(eqz_preset_10b), std::end(eqz_preset_10b), [&](const auto& row) { return preset == row.psz_name; });
        if (it == std::end(eqz_preset_10b)) return false;
        QStringList bands;
        for (int i = 0; i < it->i_band; ++i) bands.append(QString::number(std::abs(it->f_amp[i]) < 0.05f ? 0.f : it->f_amp[i], 'f', 1));
        stage(QStringLiteral("equalizer-bands"), bands.join(QLatin1Char(' ')));
        stage(QStringLiteral("equalizer-preamp"), it->f_preamp);
    }

    const auto* cfg = config_FindConfig(name.toUtf8().constData());
    if (!cfg || !CONFIG_ITEM(cfg->i_type)) return false;
    QVariant normalized;
    bool valid = true;
    switch (CONFIG_CLASS(cfg->i_type)) {
    case CONFIG_ITEM_BOOL: normalized = proposed.toBool(); break;
    case CONFIG_ITEM_INTEGER: {
        const qlonglong v = proposed.toLongLong(&valid);
        valid = valid && (cfg->min.i == cfg->max.i || (v >= cfg->min.i && v <= cfg->max.i));
        normalized = QVariant::fromValue(v); break;
    }
    case CONFIG_ITEM_FLOAT: {
        const auto converted = vlc::preferences::floatValue(proposed, cfg->min.f, cfg->max.f);
        valid = converted.has_value();
        const float v = converted.value_or(0.f);
        if (name == QLatin1String("equalizer-preamp")) valid = valid && v >= -20 && v <= 20;
        normalized = float(v); break;
    }
    case CONFIG_ITEM_STRING: normalized = proposed.toString(); break;
    default: valid = false; break;
    }
    if (valid && name == QLatin1String("startup-options") && m_ctx
        && (normalized.toString().contains(QChar::Null)
            || config_ValidateStartupOptions(VLC_OBJECT(m_ctx->getIntf()), normalized.toString().toUtf8().constData()) != VLC_SUCCESS)) {
        m_invalid.insert(name);
        m_error = qtr("Enter Hakone options only. Make sure that the option names and quotation marks are correct.");
        ++m_revision; emit changed(); return false;
    }
    if (valid && name == QLatin1String("recentplay-filter")
        && !vlc::medialibrary::HistoryFilter(normalized.toString().toStdString()).valid()) {
        m_invalid.insert(name);
        m_error = qtr("Enter a valid regular expression.");
        ++m_revision; emit changed(); return false;
    }
    if (valid && name == QLatin1String("language")) {
        const auto selected = normalized.toString();
        if (!installedInterfaceLanguage(selected) && selected != value(name).toString()) {
            m_invalid.insert(name);
            m_error = qtr("Select a language from the list.");
            ++m_revision; emit changed(); return false;
        }
    }
    if (!valid) {
        m_invalid.insert(name);
        m_error = qtr("Enter a value within the allowed range.");
        ++m_revision; emit changed(); return false;
    }
    m_invalid.remove(name);
    if (name == QLatin1String("media-library-startup")) m_libraryStartupEdited = true;
    if (name == QLatin1String("auto-preparse")) m_autoPreparseEdited = true;
    if (name == QLatin1String("one-instance")) m_singleInstanceEdited = true;
    if (isBufferingOption(name) && !value(QStringLiteral("qt-buffering-custom")).toBool())
        stage(QStringLiteral("qt-buffering-custom"), true);
    if (QStringList{QStringLiteral("hue"), QStringLiteral("brightness"), QStringLiteral("contrast"), QStringLiteral("saturation"), QStringLiteral("gamma")}.contains(name))
        stage(QStringLiteral("filter:video-filter:adjust"), true);

    const QString effect = name == QLatin1String("sharpen-sigma") ? QStringLiteral("sharpen")
        : name == QLatin1String("gradfun-radius") ? QStringLiteral("gradfun")
        : name == QLatin1String("grain-variance") ? QStringLiteral("grain")
        : name == QLatin1String("sepia-intensity") ? QStringLiteral("sepia") : QString();
    if (!effect.isEmpty())
        stage(QStringLiteral("filter:video-filter:") + effect, true);

    bool applyToPlayer = m_ctx && name == QLatin1String("qt-backdrop-blur")
        && normalized.toBool() != m_ctx->backdropBlurRequested();
    if (m_ctx && (name == QLatin1String("rate") || name == QLatin1String("random")
                  || name == QLatin1String("repeat") || name == QLatin1String("loop"))) {
        auto* playlist = m_ctx->getIntf()->p_playlist;
        vlc_playlist_Lock(playlist);
        if (name == QLatin1String("rate"))
            applyToPlayer = !qFuzzyCompare(normalized.toFloat(), vlc_player_GetRate(vlc_playlist_GetPlayer(playlist)));
        else if (name == QLatin1String("random"))
            applyToPlayer = normalized.toBool() != (vlc_playlist_GetPlaybackOrder(playlist) == VLC_PLAYLIST_PLAYBACK_ORDER_RANDOM);
        else {
            const bool repeat = name == QLatin1String("repeat") ? normalized.toBool() : value(QStringLiteral("repeat")).toBool();
            const bool loop = name == QLatin1String("loop") ? normalized.toBool() : value(QStringLiteral("loop")).toBool();
            const auto wanted = repeat ? VLC_PLAYLIST_PLAYBACK_REPEAT_CURRENT : loop
                ? VLC_PLAYLIST_PLAYBACK_REPEAT_ALL : VLC_PLAYLIST_PLAYBACK_REPEAT_NONE;
            applyToPlayer = wanted != vlc_playlist_GetPlaybackRepeat(playlist);
        }
        vlc_playlist_Unlock(playlist);
    }
    // These native caches have no getters. An explicit edit must also
    // replace a startup override when the saved default already matches.
    if (m_ctx && (name == QLatin1String("playlist-autostart") || name == QLatin1String("playlist-cork")
                  || name == QLatin1String("start-paused") || name == QLatin1String("input-repeat")
                  || name == QLatin1String("play-and-stop") || name == QLatin1String("play-and-exit")
                  || name == QLatin1String("play-and-pause")))
        applyToPlayer = true;
    if (m_ctx && name == QLatin1String("language"))
        if (auto* translator = InterfaceTranslator::instance())
            applyToPlayer = normalized.toString() != translator->requestedLanguage();
    if (m_ctx && (name == QLatin1String("qt-error-dialogs") || name == QLatin1String("qt-minor-warnings") || name == QLatin1String("qt-verbose") || name == QLatin1String("playlist-tree") || name == QLatin1String("qt-video-autoresize") || name == QLatin1String("qt-embedded-open")))
        applyToPlayer = normalized.toBool() != value(name).toBool();
    if (m_ctx && name == QLatin1String("qt-system-tray"))
        applyToPlayer = normalized.toBool() != value(name).toBool();
    if (m_ctx && (name == QLatin1String("qt-notification") || name == QLatin1String("qt-notification-timeout")))
        applyToPlayer = normalized.toLongLong() != value(name).toLongLong();
    if (m_ctx && (name == QLatin1String("qt-opacity") || name == QLatin1String("qt-fs-opacity")))
        applyToPlayer = !qFuzzyCompare(normalized.toFloat(), value(name).toFloat());
    if (m_ctx && name == QLatin1String("qt-fs-sensitivity"))
        applyToPlayer = normalized.toLongLong() != value(name).toLongLong();
    if (m_ctx && (name == QLatin1String("audio-visual") || name == QLatin1String("effect-list")))
        applyToPlayer = normalized.toString() != value(name).toString();
    if (m_ctx && name == QLatin1String("qt-compact-menus"))
        applyToPlayer = normalized.toBool() != m_ctx->compactMenus();
    if (m_ctx && name == QLatin1String("qt-discover-cast-devices"))
        applyToPlayer = normalized.toBool() != value(name).toBool();
    if (m_ctx && name == QLatin1String("metadata-network-access"))
        applyToPlayer = normalized.toBool()
            != var_InheritBool(m_ctx->getIntf(), "metadata-network-access");
    if (m_ctx && name == QLatin1String("recursive"))
        applyToPlayer = normalized.toString() != value(name).toString();
    if (const auto category = enabledTrackCategory(name); m_ctx && category != UNKNOWN_ES) {
        auto* player = m_ctx->getIntf()->p_player;
        vlc_player_Lock(player);
        applyToPlayer = normalized.toBool() != vlc_player_IsTrackCategoryEnabled(player, category);
        vlc_player_Unlock(player);
    }
    if (m_ctx && (name == QLatin1String("sub-text-scale") || name == QLatin1String("sub-text-size-mode")
        || name == QLatin1String("sub-text-size-pixels"))) {
        auto *player = m_ctx->getIntf()->p_player;
        vlc_player_Lock(player);
        if (name == QLatin1String("sub-text-scale"))
            applyToPlayer = normalized.toUInt() != vlc_player_GetSubtitleTextScale(player);
        else if (name == QLatin1String("sub-text-size-mode"))
            applyToPlayer = normalized.toInt() != vlc_player_GetSubtitleTextSizeMode(player);
        else
            applyToPlayer = normalized.toUInt() != vlc_player_GetSubtitleTextPixelSize(player);
        vlc_player_Unlock(player);
    }
    if (m_ctx && videoOutputOptions().contains(name)) {
        auto* output = vlc_player_vout_Hold(m_ctx->getIntf()->p_player);
        if (output) {
            const auto current = outputValue(output, name);
            applyToPlayer = current.isValid() && normalized != current;
            vout_Release(output);
        }
    }
    // A shortcut can change the current size without changing saved defaults.
    if (normalized == read(name.toUtf8()) && !applyToPlayer) m_edits.remove(name);
    else m_edits.insert(name, normalized);
    if (name == QLatin1String("qt-audio-waveform") && normalized.toBool())
        stage(QStringLiteral("audio-visual"), QStringLiteral("none"));
    if ((name == QLatin1String("audio-visual") && !normalized.toString().isEmpty()
            && normalized.toString() != QLatin1String("none"))
        || (name == QLatin1String("effect-list") && value(QStringLiteral("audio-visual")) == QLatin1String("visual")))
        if (value(QStringLiteral("qt-audio-waveform")).toBool()) stage(QStringLiteral("qt-audio-waveform"), false);
    if (m_invalid.isEmpty())
        m_error.clear();
    ++m_revision;
    emit changed();
    if (m_autoSave) {
        if (name == QLatin1String("language"))
            QTimer::singleShot(0, this, [this] { if (m_autoSave && dirty()) save(); });
        else m_saveTimer.start();
    }
    if (name == QLatin1String("pitch-shift"))
        return stage(QStringLiteral("filter:audio-filter:scaletempo_pitch"), normalized.toFloat() != 0.f);
    return true;
}

void RedesignPreferences::cancelLanguageEdit(const QString& name)
{
    if (languageOption(name).isEmpty() || !m_invalid.remove(name)) return;
    if (m_invalid.isEmpty()) m_error.clear();
    ++m_revision;emit changed();
    if (m_autoSave && !m_edits.isEmpty()) m_saveTimer.start();
}

void RedesignPreferences::reset(const QString& name)
{
    if (name == QLatin1String("language")) { stage(name, QStringLiteral("auto")); return; }
    if (name == QLatin1String("redesign-postprocessing")) {
        reset(QStringLiteral("postproc-q"));
        stage(name, 0);
        return;
    }
    if (name == QLatin1String("redesign-hardware-decoding")) {
        reset(QStringLiteral("dec-dev"));
        reset(QStringLiteral("hw-dec"));
        return;
    }
    const auto data = describe(name);
    if (!data.isEmpty()) stage(name, data.value(QStringLiteral("defaultValue")));
}

void RedesignPreferences::discard()
{
    m_saveTimer.stop();
    m_presetTransaction = false; m_editsBeforePreset.clear();
    m_profileInterfacePending = false; m_profileInterface.clear();
    m_edits.clear(); m_invalid.clear(); m_error.clear(); ++m_revision; emit changed();
}

bool RedesignPreferences::save()
{
    m_saveTimer.stop();
    if (!m_ctx || !m_invalid.isEmpty()) return false;
    const bool presentationChanged = std::any_of(m_edits.keyBegin(), m_edits.keyEnd(), [](const QString& name) {
        return presetOptions().contains(name);
    });
    if (!m_applicationProfile && presentationChanged && (!value(presetOption).toString().isEmpty() || m_editPlayerPreset)) {
        auto state = playerPresetState();
        if (state.isEmpty() && m_editPlayerPreset) return playerPresetError(m_applicationProfile ? qtr("Hakone cannot read the saved profiles.") : qtr("Hakone cannot read the saved presets."));
        if (!state.isEmpty()) {
            const auto selected = state.value(QStringLiteral("selected")).toString();
            if (m_editPlayerPreset && !selected.isEmpty()) {
                auto presets = state.value(QStringLiteral("presets")).toList();
                for (auto& row : presets) {
                    auto preset = row.toMap();
                    if (preset.value(QStringLiteral("id")).toString() != selected) continue;
                    auto options = preset.value(QStringLiteral("options")).toMap();
                    const auto current = playerPresetOptions();
                    for (auto it = current.cbegin(); it != current.cend(); ++it) options.insert(it.key(), it.value());
                    preset.insert(QStringLiteral("options"), options); row = preset;
                }
                state.insert(QStringLiteral("presets"), presets);
            } else {
                auto defaults = state.value(QStringLiteral("default")).toMap();
                for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it)
                    if (presetOptions().contains(it.key())) defaults.insert(it.key(), it.value());
                state.insert(QStringLiteral("default"), defaults);
            }
            m_edits.insert(presetOption, QString::fromUtf8(QJsonDocument::fromVariant(state).toJson(QJsonDocument::Compact)));
        }
    }
    if (!updateActiveApplicationProfile()) return false;
    auto* translator = InterfaceTranslator::instance();
    const bool changeLanguage = translator && m_edits.contains(QStringLiteral("language"));
    if (changeLanguage && !translator->prepareLanguage(value(QStringLiteral("language")).toString())) {
        m_error = qtr("Hakone cannot load this language.");
        ++m_revision; emit changed(); return false;
    }
    QSettings uiSettings(m_ctx->getIntf()->mainSettings->fileName(), m_ctx->getIntf()->mainSettings->format());
    uiSettings.setFallbacksEnabled(false);
    QVariantMap previousInterface;
    if (m_profileInterfacePending) {
        if (QFileInfo(uiSettings.fileName()).isDir() || !uiSettings.isWritable()) return playerPresetError(qtr("Hakone cannot save the interface settings. Try again."));
        for (const auto& key : uiSettings.allKeys()) previousInterface.insert(key, uiSettings.value(key));
    }
    QVariantMap previous;
    for (auto it = m_edits.cbegin(); it != m_edits.cend(); ++it) {
        previous.insert(it.key(), read(it.key().toUtf8()));
        write(it.key().toUtf8(), it.value());
    }
    if (config_SaveConfigFile(m_ctx->getIntf()) != 0) {
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) write(it.key().toUtf8(), it.value());
        m_error = qtr("Hakone cannot save the preferences file. Your changes are still open.");
        ++m_revision; emit changed(); return false;
    }
    if (m_profileInterfacePending) {
        uiSettings.clear();
        for (auto it = m_profileInterface.cbegin(); it != m_profileInterface.cend(); ++it) uiSettings.setValue(it.key(), it.value());
        uiSettings.sync();
        if (uiSettings.status() != QSettings::NoError) {
            QSettings recovery(uiSettings.fileName(), uiSettings.format());
            recovery.setFallbacksEnabled(false); recovery.clear();
            for (auto it = previousInterface.cbegin(); it != previousInterface.cend(); ++it) recovery.setValue(it.key(), it.value());
            recovery.sync();
            for (auto it = previous.cbegin(); it != previous.cend(); ++it) write(it.key().toUtf8(), it.value());
            const int restored = config_SaveConfigFile(m_ctx->getIntf());
            return playerPresetError(restored || recovery.status() != QSettings::NoError
                ? qtr("Hakone cannot restore the settings file. Close Hakone. Start Hakone again. Make sure that your settings are correct.")
                : qtr("Hakone cannot save the interface settings. Try again."));
        }
    }
    applyPlaybackPreferences(m_ctx->getIntf(), &previous);
    // These controls are inherited by the current input and action dispatcher.
    auto* instance = vlc_object_instance(m_ctx->getIntf());
    if (previous.contains(QStringLiteral("metadata-network-access"))) {
        if (var_Type(instance, "metadata-network-access") == 0)
            var_Create(instance, "metadata-network-access", VLC_VAR_BOOL);
        var_SetBool(instance, "metadata-network-access", bool(config_GetInt("metadata-network-access")));
        m_ctx->getIntf()->p_mainPlayerController->refreshMetadataPreference();
    }
    if (std::any_of(previous.keyBegin(), previous.keyEnd(), [](const QString& name) {
            return name.startsWith(QLatin1String("key-")) || name.startsWith(QLatin1String("global-key-"));
        })) vlc_actions_update(VLC_OBJECT(instance));
    if (previous.contains(QStringLiteral("osd")) || previous.contains(QStringLiteral("osd-duration"))
        || previous.contains(QStringLiteral("video-title-show")) || previous.contains(QStringLiteral("video-title-timeout"))
        || previous.contains(QStringLiteral("video-title-position")))
        applyOsdPreferences(m_ctx->getIntf(), previous);
    for (const auto* option : {"qt-backdrop-blur", "qt-compact-menus", "qt-error-dialogs", "qt-minor-warnings", "qt-verbose", "qt-system-tray", "qt-video-autoresize", "playlist-tree", "qt-embedded-open"}) {
        if (!previous.contains(QLatin1String(option))) continue;
        // An explicit UI choice replaces a startup override for this interface.
        auto* intf = m_ctx->getIntf();
        if (var_Type(intf, option) == 0)
            var_Create(intf, option, VLC_VAR_BOOL);
        var_SetBool(intf, option, bool(config_GetInt(option)));
    }
    for (const auto *option : {"qt-notification", "qt-notification-timeout"}) {
        if (!previous.contains(QLatin1String(option))) continue;
        auto *intf = m_ctx->getIntf();
        if (var_Type(intf, option) == 0) var_Create(intf, option, VLC_VAR_INTEGER);
        var_SetInteger(intf, option, config_GetInt(option));
    }
    if (previous.contains(QStringLiteral("qt-system-tray")))
        m_ctx->refreshSystray();
    if (previous.contains(QStringLiteral("qt-opacity")))
        applyWindowOpacity(m_ctx);
    applyFullscreenControlPreferences(m_ctx->getIntf(),
        previous.contains(QStringLiteral("qt-fs-opacity")), previous.contains(QStringLiteral("qt-fs-sensitivity")));
    if (previous.contains(QStringLiteral("qt-verbose")))
        updateVlcQtMessageHandler(var_InheritBool(m_ctx->getIntf(), "qt-verbose"));
    if (previous.contains(QStringLiteral("qt-error-dialogs")) || previous.contains(QStringLiteral("qt-minor-warnings")))
        if (auto* errors = DialogErrorModel::getInstance<false>())
            errors->refreshNotificationPreference();
    if (previous.contains(QStringLiteral("qt-discover-cast-devices")))
        m_ctx->getIntf()->p_mainPlayerController->getRendererManager()->setDiscoveryEnabled(
            bool(config_GetInt("qt-discover-cast-devices")));
    if (previous.contains(QStringLiteral("recursive"))) {
        auto* playlist = m_ctx->getIntf()->p_playlist;
        vlc_playlist_Lock(playlist);
        vlc_playlist_SetPreparseDepth(playlist, directoryDepth(read("recursive").toString()));
        vlc_playlist_Unlock(playlist);
    }
    for (const auto& name : {QStringLiteral("mouse-pause-policy"), QStringLiteral("hotkeys-x-wheel-mode"),
                            QStringLiteral("hotkeys-y-wheel-mode"), QStringLiteral("hotkeys-wheel-direction")}) {
        if (!previous.contains(name)) continue;
        const QByteArray bytes = name.toUtf8();
        const int type = name == QLatin1String("mouse-pause-policy") ? VLC_VAR_STRING
            : name == QLatin1String("hotkeys-wheel-direction") ? VLC_VAR_BOOL : VLC_VAR_INTEGER;
        if (var_Type(instance, bytes.constData()) == 0) var_Create(instance, bytes.constData(), type);
        if (type == VLC_VAR_STRING) var_SetString(instance, bytes.constData(), value(name).toString().toUtf8().constData());
        else if (type == VLC_VAR_BOOL) var_SetBool(instance, bytes.constData(), value(name).toBool());
        else var_SetInteger(instance, bytes.constData(), value(name).toLongLong());
    }

    // Update cached player preferences only after persistence succeeds.
    auto* player = m_ctx->getIntf()->p_player;
    // Language preferences are cached by the player. Updating the config alone
    // would leave subsequent episodes using the language from player creation.
    vlc_player_Lock(player);
    for (const auto* option : {"audio", "video", "spu"}) {
        const auto name = qfu(option);
        if (previous.contains(name))
            vlc_player_SetTrackCategoryEnabled(player, enabledTrackCategory(name), value(name).toBool());
    }
    if (previous.contains(QStringLiteral("audio-desync")))
        vlc_player_SetAudioDelay(player, VLC_TICK_FROM_MS(value(QStringLiteral("audio-desync")).toLongLong()), VLC_PLAYER_WHENCE_ABSOLUTE);
    if (previous.contains(QStringLiteral("sub-delay")) || previous.contains(QStringLiteral("sub-delay-ms")))
        vlc_player_SetSubtitleDelay(player, VLC_TICK_FROM_MS(value(QStringLiteral("redesign-subtitle-delay")).toLongLong()), VLC_PLAYER_WHENCE_ABSOLUTE);
    if (previous.contains(QStringLiteral("sub-fps")))
        vlc_player_SetAssociatedSubsFPS(player, value(QStringLiteral("sub-fps")).toFloat());
    if (previous.contains(QStringLiteral("sub-text-scale")))
        vlc_player_SetSubtitleTextScale(player, value(QStringLiteral("sub-text-scale")).toUInt());
    if (previous.contains(QStringLiteral("sub-text-size-mode")))
        vlc_player_SetSubtitleTextSizeMode(player, static_cast<vlc_player_subtitle_size_mode>(value(QStringLiteral("sub-text-size-mode")).toInt()));
    if (previous.contains(QStringLiteral("sub-text-size-pixels")))
        vlc_player_SetSubtitleTextPixelSize(player, value(QStringLiteral("sub-text-size-pixels")).toUInt());
    for (const auto category : {AUDIO_ES, SPU_ES}) {
        const QString option = category == AUDIO_ES ? QStringLiteral("audio-language") : QStringLiteral("sub-language");
        if (previous.contains(option)) {
            const auto language = value(option).toString().toUtf8();
            vlc_player_SelectCategoryLanguage(player, category, language.constData());
        }
    }
    vlc_player_Unlock(player);
    if (std::any_of(videoOutputOptions().cbegin(), videoOutputOptions().cend(),
                    [&](const auto& name) { return previous.contains(name); })) {
        size_t count = 0;
        auto** outputs = vlc_player_vout_HoldAll(player, &count);
        for (size_t i = 0; i < count; ++i) {
            for (const auto& name : videoOutputOptions()) {
                if (!previous.contains(name)) continue;
                const auto bytes = name.toUtf8();
                const int type = config_GetType(bytes.constData()) & VLC_VAR_CLASS;
                if (var_Type(outputs[i], bytes.constData()) == 0)
                    // Filter proxy callbacks use ISCOMMAND to find and remove
                    // mappings when the chain changes. Keep that contract even
                    // if the settings editor creates the mapping first.
                    var_Create(outputs[i], bytes.constData(), type | VLC_VAR_ISCOMMAND);
                const auto current = value(name);
                switch (type) {
                case VLC_VAR_BOOL: var_SetBool(outputs[i], bytes.constData(), current.toBool()); break;
                case VLC_VAR_INTEGER: var_SetInteger(outputs[i], bytes.constData(), current.toLongLong()); break;
                case VLC_VAR_FLOAT: var_SetFloat(outputs[i], bytes.constData(), current.toFloat()); break;
                case VLC_VAR_STRING: var_SetString(outputs[i], bytes.constData(), current.toString().toUtf8().constData()); break;
                }
            }
            // marq-file is inherited at creation, unlike the command-backed
            // overlay options. Reload its source after a successful save.
            if (previous.contains(QStringLiteral("marq-file"))
                && !previous.contains(QStringLiteral("sub-source"))) {
                const auto sources = outputValue(outputs[i], QStringLiteral("sub-source")).toString().split(QLatin1Char(':'));
                const auto legacy = outputValue(outputs[i], QStringLiteral("sub-filter")).toString().split(QLatin1Char(':'));
                if (sources.contains(QStringLiteral("marq")))
                    var_TriggerCallback(outputs[i], "sub-source");
                else if (legacy.contains(QStringLiteral("marq")))
                    var_TriggerCallback(outputs[i], "sub-filter");
            }
            vout_Release(outputs[i]);
        }
        free(outputs);
    }
    if (previous.contains(QStringLiteral("sub-margin")) || previous.contains(QStringLiteral("subsdelay-factor"))
        || previous.contains(QStringLiteral("subsdelay-mode")) || previous.contains(QStringLiteral("sub-filter"))) {
        size_t count = 0;
        auto** outputs = vlc_player_vout_HoldAll(player, &count);
        for (size_t i = 0; i < count; ++i) {
            // Set duration parameters before loading its filter. Existing
            // filters receive changes through the native command proxies.
            for (const auto& name : {QStringLiteral("subsdelay-factor"), QStringLiteral("subsdelay-mode"),
                                   QStringLiteral("sub-margin"), QStringLiteral("sub-filter")}) {
                if (!previous.contains(name)) continue;
                const auto bytes = name.toUtf8();
                const int type = config_GetType(bytes.constData()) & VLC_VAR_CLASS;
                if (var_Type(outputs[i], bytes.constData()) == 0)
                    var_Create(outputs[i], bytes.constData(), type);
                const auto current = value(name);
                if (type == VLC_VAR_FLOAT) var_SetFloat(outputs[i], bytes.constData(), current.toFloat());
                else if (type == VLC_VAR_STRING) var_SetString(outputs[i], bytes.constData(), current.toString().toUtf8().constData());
                else var_SetInteger(outputs[i], bytes.constData(), current.toLongLong());
            }
            vout_Release(outputs[i]);
        }
        free(outputs);
    }
    if (std::any_of(previous.keyBegin(), previous.keyEnd(), subtitleStyleOption))
        vlc_subpicture_InvalidateTextStyle();
    const bool deviceChanged = previous.contains(QStringLiteral("audio-device-default"))
        || previous.contains(QStringLiteral("audio-device-default-module"))
        || previous.contains(audioOutputModule() + QStringLiteral("-audio-device"));
    if (previous.contains(QStringLiteral("volume-save")) || deviceChanged || previous.contains(QStringLiteral("mix-mode"))
        || previous.contains(QStringLiteral("stereo-mode")) || previous.contains(QStringLiteral("audio-spatial"))
        || previous.contains(QStringLiteral("audio-spatial-mode"))) {
        audio_output_t* output = vlc_player_aout_Hold(player);
        if (output) {
            for (const auto& option : {QStringLiteral("mix-mode"), QStringLiteral("stereo-mode"), QStringLiteral("audio-spatial-mode")})
                if (previous.contains(option)) var_SetInteger(output, option.toUtf8().constData(), value(option).toLongLong());
            if (previous.contains(QStringLiteral("audio-spatial")))
                var_SetBool(output, "audio-spatial", value(QStringLiteral("audio-spatial")).toBool());
            if (previous.contains(QStringLiteral("volume-save"))) {
                if (var_Type(output, "volume-save") == 0) var_Create(output, "volume-save", VLC_VAR_BOOL);
                var_SetBool(output, "volume-save", value(QStringLiteral("volume-save")).toBool());
            }
            if (deviceChanged) {
                const QByteArray id = value(QStringLiteral("redesign-audio-device")).toString().toUtf8();
                aout_DeviceSet(output, id.isEmpty() ? nullptr : id.constData());
            }
            aout_Release(output);
        }
    }
    if (previous.contains(QStringLiteral("audio-filter"))) {
        auto before = previous.value(QStringLiteral("audio-filter")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (const auto output = vlc_player_aout_Hold(player)) {
            // Native effects can differ from saved defaults. Apply an explicit
            // saved chain against what is playing, so removed effects stop.
            char* current = var_GetString(output, "audio-filter");
            before = qfu(current ? current : "").split(QLatin1Char(':'), Qt::SkipEmptyParts);
            free(current); aout_Release(output);
        }
        const auto after = value(QStringLiteral("audio-filter")).toString().split(QLatin1Char(':'), Qt::SkipEmptyParts);
        for (const auto& filter : before)
            if (!after.contains(filter)) vlc_player_aout_EnableFilter(player, filter.toUtf8().constData(), false);
        for (const auto& filter : after)
            if (!before.contains(filter)) vlc_player_aout_EnableFilter(player, filter.toUtf8().constData(), true);
    }
    if (previous.contains(QStringLiteral("audio-visual")) || previous.contains(QStringLiteral("effect-list")))
        applyVisualizationPreferences(m_ctx->getIntf(), previous.contains(QStringLiteral("audio-visual")),
                                       previous.contains(QStringLiteral("effect-list")));
    const QStringList equalizerOptions{QStringLiteral("equalizer-preset"), QStringLiteral("equalizer-bands"), QStringLiteral("equalizer-preamp"), QStringLiteral("equalizer-2pass")};
    if (std::any_of(equalizerOptions.cbegin(), equalizerOptions.cend(), [&](const auto& name) { return previous.contains(name); })) {
        audio_output_t* output = vlc_player_aout_Hold(player);
        if (output) {
            for (const auto& name : equalizerOptions) {
                const auto current = value(name);
                const QByteArray bytes = name.toUtf8();
                if (name == QLatin1String("equalizer-preamp")) var_SetFloat(output, bytes.constData(), current.toFloat());
                else if (name == QLatin1String("equalizer-2pass")) var_SetBool(output, bytes.constData(), current.toBool());
                else if (name != QLatin1String("equalizer-preset") || !current.toString().isEmpty()) var_SetString(output, bytes.constData(), current.toString().toUtf8().constData());
            }
            aout_Release(output);
        }
    }
    if (std::any_of(audioFilterFloatOptions().cbegin(), audioFilterFloatOptions().cend(), [&](const auto& name) { return previous.contains(name); })) {
        audio_output_t* output = vlc_player_aout_Hold(player);
        if (output) {
            for (const auto& name : audioFilterFloatOptions()) {
                if (!previous.contains(name)) continue;
                const QByteArray bytes = name.toUtf8();
                if (var_Type(output, bytes.constData()) == 0) var_Create(output, bytes.constData(), VLC_VAR_FLOAT);
                var_SetFloat(output, bytes.constData(), value(name).toFloat());
            }
            aout_Release(output);
        }
    }
    const auto passthrough = audioPassthroughOption(audioOutputModule());
    if (!passthrough.isEmpty() && previous.contains(passthrough)) {
        audio_output_t* output = vlc_player_aout_Hold(player);
        if (output) {
            const auto bytes = passthrough.toUtf8();
            const bool boolean = config_GetType(bytes.constData()) == CONFIG_ITEM_BOOL;
            if (var_Type(output, bytes.constData()) == 0)
                var_Create(output, bytes.constData(), boolean ? VLC_VAR_BOOL : VLC_VAR_INTEGER);
            if (boolean) var_SetBool(output, bytes.constData(), value(passthrough).toBool());
            else var_SetInteger(output, bytes.constData(), value(passthrough).toLongLong());
            // A compressed stream can require a different decoder after the
            // output policy changes, even when the device is unchanged.
            aout_RestartRequest(output, true);
            aout_Release(output);
        }
    }
    if (previous.contains(QStringLiteral("audio-resampler"))
        || std::any_of(std::begin(resamplingQualities), std::end(resamplingQualities), [&](const auto& quality) {
            return previous.contains(qfu(quality.option));
        })) {
        audio_output_t* output = vlc_player_aout_Hold(player);
        if (output) {
            if (var_Type(output, "audio-resampler") == 0) var_Create(output, "audio-resampler", VLC_VAR_STRING);
            var_SetString(output, "audio-resampler", value(QStringLiteral("audio-resampler")).toString().toUtf8().constData());
            for (const auto& quality : resamplingQualities) {
                if (!previous.contains(qfu(quality.option))) continue;
                if (var_Type(output, quality.option) == 0) var_Create(output, quality.option, VLC_VAR_INTEGER);
                var_SetInteger(output, quality.option, value(qfu(quality.option)).toLongLong());
            }
            aout_RestartRequest(output, false);
            aout_Release(output);
        }
    }
    m_ctx->reloadPrefs();
    if (m_profileInterfacePending) m_ctx->reloadFromSettings();
    m_ctx->getIntf()->preferencesResetPending = false;
    discard();
    if (changeLanguage) translator->activatePreparedLanguage();
    return true;
}

bool RedesignPreferences::resetAll()
{
    if (!m_ctx) return false;
    auto* translator = InterfaceTranslator::instance();
    if (translator && !translator->prepareLanguage(QStringLiteral("auto"))) {
        m_error = qtr("Hakone cannot load this language.");
        ++m_revision; emit changed(); return false;
    }
    m_saveTimer.stop();
    auto* intf = m_ctx->getIntf();
    QSettings uiSettings(intf->mainSettings->fileName(), intf->mainSettings->format());
    uiSettings.setFallbacksEnabled(false);
    QVariantMap previousUi;
    for (const auto& key : uiSettings.allKeys()) previousUi.insert(key, uiSettings.value(key));

    QVariantMap previous;
    size_t count = 0;
    module_t** modules = module_list_get(&count);
    for (size_t i = 0; i < count; ++i) {
        unsigned size = 0;
        module_config_t* configs = module_config_get(modules[i], &size);
        for (unsigned j = 0; j < size; ++j) {
            if (!CONFIG_ITEM(configs[j].i_type) || !configs[j].psz_name) continue;
            const auto current = read(configs[j].psz_name);
            if (current.isValid()) previous.insert(qfu(configs[j].psz_name), current);
        }
        module_config_free(configs);
    }
    module_list_free(modules);
    const auto restore = [&]() {
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) write(it.key().toUtf8(), it.value());
    };
    const auto failed = [&]() {
        m_error = qtr("Hakone cannot save the default settings. Your settings did not change.");
        ++m_revision; emit changed(); return false;
    };
#ifdef _WIN32
    QSettings language(QStringLiteral("HKEY_CURRENT_USER\\Software\\Hakone"), QSettings::NativeFormat);
    const bool hasLanguage = language.contains(QStringLiteral("Lang"));
    const QVariant previousLanguage = language.value(QStringLiteral("Lang"));
    if (hasLanguage && !language.isWritable()) return failed();
#endif
    const auto rollback = [&]() {
        QSettings recovery(uiSettings.fileName(), uiSettings.format());
        recovery.setFallbacksEnabled(false);
        recovery.clear();
        for (auto it = previousUi.cbegin(); it != previousUi.cend(); ++it) recovery.setValue(it.key(), it.value());
        recovery.sync();
        restore();
        const int restored = config_SaveConfigFile(intf);
        if (restored != 0 || recovery.status() != QSettings::NoError) {
            m_error = qtr("Hakone cannot restore the settings file. Close Hakone. Start Hakone again. Make sure that your settings are correct.");
            ++m_revision; emit changed(); return false;
        }
        return failed();
    };
    if (!uiSettings.isWritable()) return failed();
    config_ResetAll();
    // Make the system-language reset explicit so a legacy platform choice
    // cannot return when the new preference has its empty migration default.
    config_PutPsz("language", "auto");
    if (config_SaveConfigFile(intf) != 0) {
        restore();
        return failed();
    }
    uiSettings.clear();
    uiSettings.sync();
    if (uiSettings.status() != QSettings::NoError) return rollback();
#ifdef _WIN32
    if (hasLanguage) {
        language.remove(QStringLiteral("Lang"));
        language.sync();
        if (language.status() != QSettings::NoError) {
            language.setValue(QStringLiteral("Lang"), previousLanguage);
            language.sync();
            return rollback();
        }
    }
#endif
    intf->preferencesResetPending = true;
    for (const auto* option : {"qt-backdrop-blur", "qt-compact-menus", "qt-error-dialogs", "qt-minor-warnings", "qt-verbose", "qt-system-tray", "qt-video-autoresize", "playlist-tree", "qt-embedded-open"}) {
        if (var_Type(intf, option) == 0)
            var_Create(intf, option, VLC_VAR_BOOL);
        var_SetBool(intf, option, bool(config_GetInt(option)));
    }
    for (const auto *option : {"qt-notification", "qt-notification-timeout"}) {
        if (var_Type(intf, option) == 0) var_Create(intf, option, VLC_VAR_INTEGER);
        var_SetInteger(intf, option, config_GetInt(option));
    }
    m_ctx->refreshSystray();
    applyPlaybackPreferences(intf);
    applyVisualizationPreferences(intf, true, true);
    applyWindowOpacity(m_ctx);
    applyFullscreenControlPreferences(intf, true, true);
    updateVlcQtMessageHandler(var_InheritBool(intf, "qt-verbose"));
    if (auto* errors = DialogErrorModel::getInstance<false>())
        errors->refreshNotificationPreference();
    intf->p_mainPlayerController->getRendererManager()->setDiscoveryEnabled(
        bool(config_GetInt("qt-discover-cast-devices")));
    vlc_playlist_Lock(intf->p_playlist);
    vlc_playlist_SetPreparseDepth(intf->p_playlist, directoryDepth(read("recursive").toString()));
    vlc_playlist_Unlock(intf->p_playlist);
    applyOsdPreferences(intf, previous);
    vlc_subpicture_InvalidateTextStyle();
    vlc_player_Lock(intf->p_player);
    for (const auto* option : {"audio", "video", "spu"})
        vlc_player_SetTrackCategoryEnabled(intf->p_player, enabledTrackCategory(qfu(option)), bool(config_GetInt(option)));
    for (const auto category : {AUDIO_ES, SPU_ES}) {
        char* language = config_GetPsz(category == AUDIO_ES ? "audio-language" : "sub-language");
        vlc_player_SelectCategoryLanguage(intf->p_player, category, language);
        free(language);
    }
    vlc_player_SetSubtitleTextScale(intf->p_player, config_GetInt("sub-text-scale"));
    vlc_player_SetSubtitleTextSizeMode(intf->p_player, static_cast<vlc_player_subtitle_size_mode>(config_GetInt("sub-text-size-mode")));
    vlc_player_SetSubtitleTextPixelSize(intf->p_player, config_GetInt("sub-text-size-pixels"));
    vlc_player_Unlock(intf->p_player);
    m_ctx->reloadPrefs();
    m_ctx->reloadFromSettings();
    discard();
    if (translator) translator->activatePreparedLanguage();
    return true;
}

QVariantList RedesignPreferences::pluginCatalog() const
{
    size_t count = 0;
    module_t **modules = module_list_get(&count);
    QVariantList plugins;
    for (size_t i = 0; i < count; ++i) {
        const auto capability = qfu(module_get_capability(modules[i]));
        if (capability.isEmpty()) continue;
        plugins.append(QVariantMap{
            {QStringLiteral("name"), qfu(module_GetLongName(modules[i]))},
            {QStringLiteral("type"), capability},
            {QStringLiteral("priority"), module_get_score(modules[i])},
            {QStringLiteral("identifier"), qfu(module_get_object(modules[i]))}});
    }
    module_list_free(modules);
    return plugins;
}

QVariantList RedesignPreferences::catalog(const QString& category, const QString& search) const
{
    QVariantList result;
    for (const auto& entry : m_catalog) {
        if (entry.name == QLatin1String("sub-delay-ms")) continue;
        if (!search.isEmpty()) {
            if (!entry.name.contains(search, Qt::CaseInsensitive) && !entry.title.contains(search, Qt::CaseInsensitive)) continue;
        } else if (entry.category != category) continue;
        const bool track = entry.name == QLatin1String("audio-track") || entry.name == QLatin1String("sub-track");
        const bool language = entry.name == QLatin1String("audio-language") || entry.name == QLatin1String("sub-language");
        const auto name = entry.name == QLatin1String("sub-delay") ? QStringLiteral("redesign-subtitle-delay")
            : track || language ? QStringLiteral("redesign-") + entry.name : entry.name;
        result.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("title"), entry.title}});
    }
    return result;
}

QString RedesignPreferences::choosePath(const QString& name)
{
    const auto* cfg = config_FindConfig(name.toUtf8().constData());
    if (!cfg) return {};
    const QString initial = value(name).toString();
    switch (cfg->i_type) {
    case CONFIG_ITEM_DIRECTORY: return QFileDialog::getExistingDirectory(nullptr, qtr("Select folder"), initial);
    case CONFIG_ITEM_SAVEFILE: return QFileDialog::getSaveFileName(nullptr, qtr("Select file"), initial);
    case CONFIG_ITEM_LOADFILE: return QFileDialog::getOpenFileName(nullptr, qtr("Select file"), initial);
    default: return {};
    }
}

QUrl RedesignPreferences::configurationFolder() const
{
    if (!m_ctx) return {};
    char* custom = var_InheritString(m_ctx->getIntf(), "config");
    QString folder;
    if (custom && *custom) folder = QFileInfo(qfu(custom)).absolutePath();
    free(custom);
    if (folder.isEmpty()) {
        char* path = config_GetUserDir(VLC_CONFIG_DIR);
        folder = qfu(path ? path : "");
        free(path);
    }
    if (folder.isEmpty() || !QDir().mkpath(folder)) return {};
    return QUrl::fromLocalFile(folder);
}

QString RedesignPreferences::chooseColor(const QString& initial)
{
    const QColor color = QColorDialog::getColor(QColor(initial), nullptr, qtr("Select color"));
    return color.isValid() ? color.name() : QString();
}

QVariantList RedesignPreferences::equalizerBands() const
{
    const auto custom = value(QStringLiteral("equalizer-bands")).toString().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    const QByteArray name = value(QStringLiteral("equalizer-preset")).toString().toUtf8();
    const auto preset = std::find_if(std::begin(eqz_preset_10b), std::end(eqz_preset_10b), [&](const auto& row) { return name == row.psz_name; });
    QVariantList result;
    for (int i = 0; i < 10; ++i) {
        bool valid = false;
        double gain = i < custom.size() ? custom[i].toDouble(&valid) : 0;
        if (!valid || !std::isfinite(gain)) gain = preset != std::end(eqz_preset_10b) ? preset->f_amp[i] : 0;
        result.append(std::abs(gain) < 0.05 ? 0.0 : std::clamp(gain, -20.0, 20.0));
    }
    return result;
}

bool RedesignPreferences::setEqualizerBand(int index, double gain)
{
    if (index < 0 || index >= 10 || !std::isfinite(gain) || gain < -20 || gain > 20) return false;
    const auto values = equalizerBands();
    QStringList bands;
    for (int i = 0; i < values.size(); ++i) bands.append(QString::number(i == index ? gain : values[i].toDouble(), 'f', 1));
    if (!stage(QStringLiteral("equalizer-bands"), bands.join(QLatin1Char(' ')))) return false;
    if (!stage(QStringLiteral("filter:audio-filter:equalizer"), true)) return false;
    return stage(QStringLiteral("equalizer-preset"), QString());
}

QVariantList RedesignPreferences::shortcutCatalog() const
{
    struct Shortcut { QString name, title, group; int groupOrder, order; };
    QList<Shortcut> shortcuts;
    const QStringList basic{QStringLiteral("key-toggle-fullscreen"), QStringLiteral("key-leave-fullscreen"), QStringLiteral("key-position"), QStringLiteral("key-intf-show")};
    const QStringList playback{QStringLiteral("key-play-pause"), QStringLiteral("key-stop"), QStringLiteral("key-next"), QStringLiteral("key-prev"), QStringLiteral("key-program-sid-next"), QStringLiteral("key-program-sid-prev")};
    for (const auto& entry : m_catalog) {
        if (!entry.name.startsWith(QLatin1String("key-"))) continue;
        Shortcut row{entry.name, entry.title, qtr("Other"), 5, 1000};
        if (basic.contains(entry.name)) { row.group = qtr("Basic"); row.groupOrder = 0; row.order = basic.indexOf(entry.name); }
        else if (playback.contains(entry.name)) { row.group = qtr("Playback"); row.groupOrder = 1; row.order = playback.indexOf(entry.name); }
        else if (entry.name.contains(QLatin1String("sub"))) { row.group = qtr("Subtitles"); row.groupOrder = 4; }
        else if (entry.name.contains(QLatin1String("audio")) || entry.name.contains(QLatin1String("vol-"))) { row.group = qtr("Audio"); row.groupOrder = 2; }
        else if (entry.name.contains(QLatin1String("aspect")) || entry.name.contains(QLatin1String("crop")) || entry.name.contains(QLatin1String("zoom")) || entry.name.contains(QLatin1String("snapshot")) || entry.name.contains(QLatin1String("deinterlace"))) { row.group = qtr("Video"); row.groupOrder = 3; }
        else if (entry.name.contains(QLatin1String("jump")) || entry.name.contains(QLatin1String("rate")) || entry.name.contains(QLatin1String("frame"))) { row.group = qtr("Playback"); row.groupOrder = 1; }
        if (entry.name == QLatin1String("key-toggle-fullscreen")) row.title = qtr("Toggle fullscreen");
        else if (entry.name == QLatin1String("key-leave-fullscreen")) row.title = qtr("Exit fullscreen / close dialog");
        else if (entry.name == QLatin1String("key-position")) row.title = qtr("Show playback position and time");
        else if (entry.name == QLatin1String("key-intf-show")) row.title = qtr("Show interface controls in fullscreen");
        else if (entry.name == QLatin1String("key-play-pause")) row.title = qtr("Play / pause");
        else if (entry.name == QLatin1String("key-stop")) row.title = qtr("Stop playback");
        else if (entry.name == QLatin1String("key-next")) row.title = qtr("Next track");
        else if (entry.name == QLatin1String("key-prev")) row.title = qtr("Previous track");
        else if (entry.name == QLatin1String("key-program-sid-next")) row.title = qtr("Next program / Service ID");
        else if (entry.name == QLatin1String("key-program-sid-prev")) row.title = qtr("Previous program / Service ID");
        shortcuts.append(row);
    }
    std::sort(shortcuts.begin(), shortcuts.end(), [](const Shortcut& a, const Shortcut& b) {
        if (a.groupOrder != b.groupOrder) return a.groupOrder < b.groupOrder;
        if (a.order != b.order) return a.order < b.order;
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    QVariantList result;
    for (const auto& row : shortcuts) {
        const QString global = QStringLiteral("global-") + row.name;
        result.append(QVariantMap{{QStringLiteral("name"), row.name}, {QStringLiteral("title"), row.title},
            {QStringLiteral("group"), row.group}, {QStringLiteral("globalName"), config_FindConfig(global.toUtf8().constData()) ? global : QString()}});
    }
    return result;
}

QString RedesignPreferences::shortcutForKey(int key, int modifiers) const
{
    QKeyEvent event(QEvent::KeyPress, key, Qt::KeyboardModifiers::fromInt(modifiers));
    return VLCKeyToString(qtEventToVLCKey(&event), false);
}

QVariantList RedesignPreferences::shortcutConflicts(const QString& name, const QString& shortcut) const
{
    QVariantList conflicts;
    if (shortcut.isEmpty()) return conflicts;
    const bool global = name.startsWith(QLatin1String("global-key-"));
    for (const auto& entry : m_catalog) {
        if (entry.name == name || entry.name.startsWith(QLatin1String("global-key-")) != global) continue;
        if (!entry.name.startsWith(global ? QLatin1String("global-key-") : QLatin1String("key-"))) continue;
        if (value(entry.name).toString().split(QLatin1Char('\t')).contains(shortcut, Qt::CaseInsensitive))
            conflicts.append(QVariantMap{{QStringLiteral("name"), entry.name}, {QStringLiteral("title"), entry.title}});
    }
    return conflicts;
}

bool RedesignPreferences::assignShortcut(const QString& name, const QString& shortcut, bool replaceConflicts)
{
    const auto* cfg = config_FindConfig(name.toUtf8().constData());
    if (!cfg || cfg->i_type != CONFIG_ITEM_KEY) return false;
    const auto conflicts = shortcutConflicts(name, shortcut);
    if (!conflicts.isEmpty() && !replaceConflicts) return false;
    for (const auto& conflict : conflicts) {
        const QString other = conflict.toMap().value(QStringLiteral("name")).toString();
        QStringList bindings = value(other).toString().split(QLatin1Char('\t'), Qt::SkipEmptyParts);
        bindings.erase(std::remove_if(bindings.begin(), bindings.end(), [&shortcut](const QString& binding) {
            return binding.compare(shortcut, Qt::CaseInsensitive) == 0;
        }), bindings.end());
        stage(other, bindings.join(QLatin1Char('\t')));
    }
    if (shortcut.isEmpty()) return stage(name, QString());
    QStringList bindings = value(name).toString().split(QLatin1Char('\t'), Qt::SkipEmptyParts);
    // Replace the displayed binding while retaining alternate and media keys.
    if (!bindings.isEmpty() && !bindings.first().startsWith(QLatin1String("Media ")))
        bindings.removeFirst();
    bindings.removeAll(shortcut);
    bindings.prepend(shortcut);
    return stage(name, bindings.join(QLatin1Char('\t')));
}
