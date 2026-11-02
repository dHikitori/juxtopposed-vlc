#include "mainui.hpp"
#include "util/interface_translator.hpp"

#include <cassert>

#include "maininterface/navigationmodel.hpp"

#include "medialibrary/medialib.hpp"
#include "medialibrary/mlqmltypes.hpp"
#include "medialibrary/mlcustomcover.hpp"
#include "medialibrary/mlalbummodel.hpp"
#include "medialibrary/mlartistmodel.hpp"
#include "medialibrary/mlaudiomodel.hpp"
#include "medialibrary/mlgenremodel.hpp"
#include "medialibrary/mlurlmodel.hpp"
#include "medialibrary/mlmediamodel.hpp"
#include "medialibrary/mlfileartwork.hpp"
#include "medialibrary/mlvideomodel.hpp"
#include "medialibrary/mlrecentmediamodel.hpp"
#include "medialibrary/mlrecentvideomodel.hpp"
#include "medialibrary/mlrecentaudiomodel.hpp"
#include "medialibrary/mlplayedartistmodel.hpp"
#include "medialibrary/mlfoldersmodel.hpp"
#include "medialibrary/mlvideogroupsmodel.hpp"
#include "medialibrary/mlvideofoldersmodel.hpp"
#include "medialibrary/mlplaylistlistmodel.hpp"
#include "medialibrary/mlplaylistmodel.hpp"
#include "medialibrary/mlplaylist.hpp"
#include "medialibrary/mlbookmarkmodel.hpp"

#include "player/player_controller.hpp"
#include "player/series_model.hpp"
#include "player/episode_metadata.hpp"
#include "player/player_controlbar_model.hpp"
#include "player/control_list_model.hpp"
#include "player/control_list_filter.hpp"
#include "player/delay_estimator.hpp"
#include "addons/addon_profile_host.hpp"
#include "addons/addon_account_setup.hpp"
#include "maininterface/addon_guest_sources.hpp"
#include "addons/addon_media_siblings.hpp"
#include <QFileDialog>
#ifdef VLC_ADDON_TEST_DRIVER
#include "addons/addon_test_driver.hpp"
#endif

#include "dialogs/toolbar/controlbar_profile_model.hpp"
#include "dialogs/toolbar/controlbar_profile.hpp"

#include "playlist/playlist_model.hpp"
#include "playlist/playlist_tree_model.hpp"
#include "playlist/playlist_controller.hpp"

#include "util/csdmenu.hpp"
#include "util/item_key_event_filter.hpp"
#include "util/imageluminanceextractor.hpp"
#include "util/keyhelper.hpp"
#include "style/systempalette.hpp"
#include "style/vlcicons.hpp"
#include "util/navigation_history.hpp"
#include "util/flickable_scroll_handler.hpp"
#include "util/kirigamiwheelhandler.hpp"
#include "util/color_svg_image_provider.hpp"
#include "util/effects_image_provider.hpp"
#include "util/vlcaccess_image_provider.hpp"
#include "util/seek_preview_image_provider.hpp"
#include "util/thumbnail_cache.hpp"
#include "util/csdbuttonmodel.hpp"
#include "util/vlctick.hpp"
#include "util/list_selection_model.hpp"
#include "util/ui_notifier.hpp"
#include "util/textureproviderobserver.hpp"
#include "util/textureproviderindirection.hpp"
#include "util/sgmanipulator.hpp"
#include "util/pointingtooltipattached.hpp"
#include "util/accessible_compat_attached.hpp"

#include "dialogs/help/aboutmodel.hpp"
#include "dialogs/dialogs_provider.hpp"
#include "dialogs/preferences/redesign_preferences.hpp"
#include "dialogs/firstrun/firstrunwizard.hpp"
#include "util/redesign_folder_model.hpp"
#include "util/subtitle_source_service.hpp"
#include "addons/addon_manager.hpp"
#include "addons/addon_catalogue_view.hpp"
#include "addons/addon_package_summaries.hpp"
#include "addons/addon_metadata_service.hpp"
#include "addons/addon_native_metadata_owner.hpp"
#include "addons/addon_accepted_identities.hpp"
#include "addons/addon_identity_choice.hpp"
#include "addons/addon_process_launch.hpp"
#include <vlc_metadata_owner.h>
#include "addon_distribution_paths.hpp"
#include "addons/addon_theme_service.hpp"
#include "addons/addon_ui_registry.hpp"
#include "addons/addon_ui_assets.hpp"
#include "addons/addon_asset_image_provider.hpp"
#include "addons/addon_player_visualizers.hpp"
#include "addons/addon_player_sessions.hpp"
#include "addons/addon_session_ui.hpp"
#include "addons/addon_visualizer_surface.hpp"
#include "addons/addon_discovery_service.hpp"
#include "addons/addon_native_queue_store.hpp"
#include "addons/addon_native_library_store.hpp"
#include "addons/addon_native_library_targets.hpp"
#include "addons/addon_player_discovery.hpp"
#include "addons/addon_discovery_source_owner.hpp"
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStyleHints>
#include <vlc_configuration.h>
#include <QQmlContext>
#include "dialogs/dialogs/dialogmodel.hpp"
#ifdef UPDATE_CHECK
#include "dialogs/help/help.hpp" // UpdateModel
#endif
#include "util/vlchotkeyconverter.hpp"

#include "network/networkmediamodel.hpp"
#include "network/networkdevicemodel.hpp"
#include "network/networksourcesmodel.hpp"
#include "network/networkplaylistmodel.hpp"
#include "network/addonsmodel.hpp"
#include "network/standardpathmodel.hpp"

#include "menus/qml_menu_wrapper.hpp"

#include "widgets/native/csdthemeimage.hpp"
#include "widgets/native/navigation_attached.hpp"
#include "widgets/native/viewblockingrectangle.hpp"
#include "widgets/native/softwarebackdropblur.hpp"
#if QT_VERSION < QT_VERSION_CHECK(6, 4, 0)
#include "widgets/native/doubleclickignoringitem.hpp"
#else
// QQuickItem already ignores double click, starting
// with Qt 6.4.0:
#define DoubleClickIgnoringItem QQuickItem
#endif

#include "videosurface.hpp"
#include "mainctx.hpp"
#ifdef _WIN32
#include "mainctx_win32.hpp"
#endif
#include "mainctx_submodels.hpp"

static bool g_qmlTypesAreRegistered = false;

using  namespace vlc::playlist;

MainUI::MainUI(qt_intf_t *p_intf, MainCtx *mainCtx, QWindow* interfaceWindow,  QObject *parent)
    : QObject(parent)
    , m_intf(p_intf)
    , m_mainCtx(mainCtx)
    , m_interfaceWindow(interfaceWindow)
{
    assert(m_intf);
    assert(m_mainCtx);
    assert(m_interfaceWindow);

    registerQMLTypes();

    // FIXME: Since Qt 6.6, Qt follows a different approach for scrolling. This new approach has
    //        caused a lot of frustration (QTBUG-116388, QTBUG-129948, QTBUG-120038). However,
    //        the previous scrolling behavior was not perfect either, so we have not been opting
    //        in to use the previous behavior. That being said, the new approach broke scrolling
    //        with views that has sections completely, so we don't have any option besides using
    //        the previous scrolling implementation. The new approach (b1766d9), `Flickable:
    //        Proportional wheel scrolling if deceleration is large` tells that the previous
    //        deceleration value was 5000. In this case, we use 10000 instead to not make scrolling
    //        as loose as it was before (which was apparently the main reason of coming up with the
    //        new approach). Setting this environment variable to any value lower than 15000
    //        reportedly (and evidently) brings back the old behavior, and does not continue
    //        using the new approach with the deceleration set (which is what we want to fix
    //        scrolling when there are sections).
    if constexpr (QT_VERSION >= QT_VERSION_CHECK(6, 6, 0))
    {
        const auto envFlickableWheelDeceleration = "QT_QUICK_FLICKABLE_WHEEL_DECELERATION";
        if (qEnvironmentVariableIsEmpty(envFlickableWheelDeceleration))
            qputenv(envFlickableWheelDeceleration, QByteArrayLiteral("10000"));
    }
}

MainUI::~MainUI()
{
    vlc::addons::setHelperLaunchObserver({});
    for (auto* manager:findChildren<vlc::addons::AddonManager*>(QString(),Qt::FindDirectChildrenOnly)) manager->setNativeStreamAvailable(false);
    for (auto* source:findChildren<vlc::addons::AddonDiscoverySourceOwner*>(QString(),Qt::FindDirectChildrenOnly)) source->close();
    for (auto* sessions:findChildren<vlc::addons::AddonPlayerSessions*>(QString(),Qt::FindDirectChildrenOnly)) sessions->close();
    for (auto* service:findChildren<AddonMetadataService*>(QString(),Qt::FindDirectChildrenOnly)) service->shutdown();
    for (auto* queue:findChildren<vlc::addons::AddonNativeQueueStore*>(QString(),Qt::FindDirectChildrenOnly)) queue->close();
    for (auto* picker:findChildren<vlc::addons::AddonNativeLibraryTargets*>(QString(),Qt::FindDirectChildrenOnly)) picker->close();
    for (auto* library:findChildren<vlc::addons::AddonNativeLibraryStore*>(QString(),Qt::FindDirectChildrenOnly)) library->close();
    if (!m_engineBound)
    {
        // If the global engine is dead, we assume no other QML engine exists hence can clear the
        // types. Note that there may be multiple QML engines co-existing, but the global engine
        // is expected to outlive the others.
        clearQMLTypes();
    }
    else
    {
        // `QObject::destroyed()` is signalled just before the object dies, but subclass `QQmlEngine`
        // itself should be dead at that point anyway. This should prevent undefined behavior.
        connect(m_engineBound, &QObject::destroyed, &MainUI::clearQMLTypes);
    }
}

bool MainUI::setup(QQmlEngine* engine)
{
    m_engineBound = engine;
    if (auto* translator = InterfaceTranslator::instance()) translator->watchEngine(engine);

    if (m_mainCtx->hasMediaLibrary())
    {
        engine->addImageProvider(MLCustomCover::providerId, new MLCustomCover(m_mainCtx->getMediaLibrary()));
    }

#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
    engine->addImportPath(":/qt/qml");
#endif

    if (!EffectsImageProvider::instance(engine, engine))
        new EffectsImageProvider(engine);
    engine->addImageProvider(QStringLiteral("svgcolor"), new SVGColorImageImageProvider());
    engine->rootContext()->setContextProperty(QStringLiteral("redesignSvgImages"), true);
    engine->rootContext()->setContextProperty(QStringLiteral("redesignAppearance"), m_mainCtx);
    engine->rootContext()->setContextProperty(QStringLiteral("redesignThemeService"), vlc::addons::AddonThemeService::instance());
    engine->rootContext()->setContextProperty(QStringLiteral("redesignAddonUi"), findChild<vlc::addons::AddonUiRegistry*>());
    if (auto* assets = findChild<vlc::addons::AddonUiAssets*>())
        engine->addImageProvider(QStringLiteral("addonassets"), new vlc::addons::AddonAssetImageProvider(assets));
    engine->rootContext()->setContextProperty(QStringLiteral("redesignDiscoveryService"), findChild<vlc::addons::AddonDiscoveryService*>());
    engine->rootContext()->setContextProperty(QStringLiteral("redesignSeriesModel"), findChild<SeriesModel*>(QStringLiteral("redesignSeriesModel")));
    engine->rootContext()->setContextProperty(QStringLiteral("redesignMediaSiblings"), findChild<vlc::addons::AddonMediaSiblings*>(QStringLiteral("redesignMediaSiblings")));
    engine->addImageProvider(QStringLiteral("vlcaccess"), new VLCAccessImageProvider());
    engine->addImageProvider(QStringLiteral("seekpreview"), new SeekPreviewImageProvider());
    auto* thumbnails = new ThumbnailCache(ThumbnailCache::defaultLimit, engine);
    engine->addImageProvider(QLatin1String(ThumbnailCache::providerId), thumbnails->createProvider());
    engine->rootContext()->setContextProperty(QStringLiteral("redesignThumbnails"), thumbnails);

    m_component  = new QQmlComponent(engine, QStringLiteral("qrc:/qt/qml/VLC/MainInterface/MainInterface.qml"), QQmlComponent::PreferSynchronous, engine);
    if (m_component->isLoading())
    {
        msg_Warn(m_intf, "component is still loading");
    }

    if (m_component->isError())
    {
        for(auto& error: m_component->errors())
            msg_Err(m_intf, "qml loading %s %s:%u", qtu(error.description()), qtu(error.url().toString()), error.line());
        // A QML error can also come from an unavailable resource or a rejected
        // compiled unit. Report the actual errors and let startup clean up.
        msg_Err(m_intf, "Cannot load the VLC interface. See the errors above.");
        return false;
    }
    return true;
}

QQuickItem* MainUI::createRootItem()
{
    if (m_rootItem)
        return m_rootItem;

    assert(m_component);

    QObject* rootObject = m_component->create();

    if (m_component->isError())
    {
        for(auto& error: m_component->errors())
            msg_Err(m_intf, "qml loading %s %s:%u", qtu(error.description()), qtu(error.url().toString()), error.line());
        goto end;
    }

    if (rootObject == nullptr)
    {
        msg_Err(m_intf, "unable to create main interface, no root item");
        goto end;
    }
    m_rootItem = qobject_cast<QQuickItem*>(rootObject);
    if (!m_rootItem)
    {
        msg_Err(m_intf, "unexpected type of qml root item");
        goto end;
    }

end:
    delete m_component; // No need to have the component around anymore.
    return m_rootItem;
}

void MainUI::registerQMLTypes()
{
    assert(!g_qmlTypesAreRegistered);

    {
        const char* uri = "VLC.MainInterface";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.MainInterface
        qmlRegisterSingletonInstance<MainCtx>(uri, versionMajor, versionMinor, "MainCtx", m_mainCtx);
#ifdef _WIN32
        assert(dynamic_cast<MainCtxWin32*>(m_mainCtx));
        qmlRegisterAnonymousType<MainCtxWin32>(uri, versionMajor);
#endif
        qmlRegisterTypesAndRevisions<SearchCtx>(uri, versionMajor);
        qmlRegisterTypesAndRevisions<SortCtx>(uri, versionMajor);
        qmlRegisterUncreatableType<UINotifier>(uri, versionMajor, versionMinor, "UINotifier", "");
        qmlRegisterSingletonInstance<UINotifier>(uri, versionMajor, versionMinor, "UINotifier", new UINotifier(m_mainCtx, m_mainCtx));
        qmlRegisterSingletonInstance<NavigationHistory>(uri, versionMajor, versionMinor, "History", new NavigationHistory(this));
        qmlRegisterType<VideoSurface>(uri, versionMajor, versionMinor, "VideoSurface");
        qmlRegisterAnonymousType<VideoBackdropProvider>(uri, versionMajor);
        qmlRegisterType<VideoBackdrop>(uri, versionMajor, versionMinor, "VideoBackdrop");
        qmlRegisterUncreatableType<BaseModel>( uri, versionMajor, versionMinor, "BaseModel", "Base Model is uncreatable." );
        qmlRegisterUncreatableType<VLCVarChoiceModel>(uri, versionMajor, versionMinor, "VLCVarChoiceModel", "generic variable with choice model" );
        qmlRegisterUncreatableType<CSDButton>(uri, versionMajor, versionMinor, "CSDButton", "");
        qmlRegisterUncreatableType<CSDButtonModel>(uri, versionMajor, versionMinor, "CSDButtonModel", "has CSD buttons and provides for communicating CSD events between UI and backend");
        qmlRegisterTypesAndRevisions<CSDMenu>( uri, versionMajor);
        qmlRegisterUncreatableType<NavigationAttached>( uri, versionMajor, versionMinor, "Navigation", "Navigation is only available via attached properties");
        qmlRegisterTypesAndRevisions<AboutModel>( uri, versionMajor );
#ifdef UPDATE_CHECK
        qmlRegisterSingletonInstance<UpdateModel>( uri, versionMajor, versionMinor, "UpdateModel", m_mainCtx->getUpdateModel() );
#endif

        qmlRegisterTypesAndRevisions<NavigationModel>( uri, versionMajor);
        qmlRegisterTypesAndRevisions<NavigationBarContextMenu>( uri, versionMajor );

        // Foreign types:
        qmlRegisterTypesAndRevisions<vlc::QAbstractProxyModelForeign>(uri, versionMajor);
        qmlRegisterTypesAndRevisions<vlc::QWindowForeign>(uri, versionMajor);
        qmlRegisterTypesAndRevisions<vlc::QScreenForeign>(uri, versionMajor);
        qmlRegisterTypesAndRevisions<vlc::QSortFilterProxyModelForeign>( uri, versionMajor );
        qmlRegisterTypesAndRevisions<vlc::QMessageBoxForeign>( uri, versionMajor );
#if QT_VERSION < QT_VERSION_CHECK(6, 5, 0)
        qmlRegisterTypesAndRevisions<vlc::QAbstractItemModelForeign>(uri, versionMajor); // Qt >= 6.5.0, import `QtQml.Models`.
#endif

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Dialogs";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Dialogs
        qmlRegisterType<RedesignPreferences>(uri, versionMajor, versionMinor, "RedesignPreferences");
        qmlRegisterType<vlc::addons::AddonAccountSetup>(uri, versionMajor, versionMinor, "AddonAccountSetup");
        qmlRegisterType<FirstRunWizard>(uri, versionMajor, versionMinor, "RedesignFirstRunSetup");
        qmlRegisterType<RedesignFolderModel>(uri, versionMajor, versionMinor, "RedesignFolderModel");
        qmlRegisterType<SeriesModel>(uri, versionMajor, versionMinor, "SeriesModel");
        qmlRegisterType<EpisodeMetadata>(uri, versionMajor, versionMinor, "EpisodeMetadata");
        qmlRegisterType<VLCDialog>( uri, versionMajor, versionMinor, "VLCDialog" );
        assert(VLCDialogModel::getInstance<false>());
        qmlRegisterSingletonInstance<VLCDialogModel>(uri, versionMajor, versionMinor, "VLCDialogModel", VLCDialogModel::getInstance<false>());
        qmlRegisterUncreatableType<DialogId>( uri, versionMajor, versionMinor, "dialogId", "");
        assert(DialogsProvider::getInstance());
        qmlRegisterSingletonInstance<DialogsProvider>(uri, versionMajor, versionMinor, "DialogsProvider", DialogsProvider::getInstance());
        assert(DialogErrorModel::getInstance<false>());
        qmlRegisterSingletonInstance<DialogErrorModel>(uri, versionMajor, versionMinor, "DialogErrorModel", DialogErrorModel::getInstance<false>());

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Menus";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Menus
        qmlRegisterType<StringListMenu>( uri, versionMajor, versionMinor, "StringListMenu" );
        qmlRegisterType<SortMenu>( uri, versionMajor, versionMinor, "SortMenu" );
        qmlRegisterType<SortMenuVideo>( uri, versionMajor, versionMinor, "SortMenuVideo" );
        qmlRegisterType<SortMenuAlbums>( uri, versionMajor, versionMinor, "SortMenuAlbums" );
        qmlRegisterType<QmlGlobalMenu>( uri, versionMajor, versionMinor, "QmlGlobalMenu" );
        qmlRegisterType<QmlMenuBar>( uri, versionMajor, versionMinor, "QmlMenuBar" );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Player";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Player
        qmlRegisterUncreatableType<TrackListModel>(uri, versionMajor, versionMinor, "TrackListModel", "available tracks of a media (audio/video/sub)" );
        qmlRegisterUncreatableType<TitleListModel>(uri, versionMajor, versionMinor, "TitleListModel", "available titles of a media" );
        qmlRegisterUncreatableType<ChapterListModel>(uri, versionMajor, versionMinor, "ChapterListModel", "available chapters of a media" );
        qmlRegisterUncreatableType<ProgramListModel>(uri, versionMajor, versionMinor, "ProgramListModel", "available programs of a media" );
        assert(m_intf->p_mainPlayerController);
        qmlRegisterSingletonInstance<PlayerController>(uri, versionMajor, versionMinor, "Player", m_intf->p_mainPlayerController);
        qmlRegisterTypesAndRevisions<TimedText>(uri, versionMajor);
        char* userDirectory = config_GetUserDir(VLC_USERDATA_DIR);
        const QString dataDirectory = userDirectory ? qfu(userDirectory)
            : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        free(userDirectory);
        vlc::addons::AddonManager* addonManager = nullptr;
        AddonMetadataService* metadataService = nullptr;
        if (!dataDirectory.isEmpty()) {
            // Package and profile reads for the GUI run on background work.
            vlc::addons::AddonManager::setBackgroundReads(true);
            addonManager = new vlc::addons::AddonManager(QDir(dataDirectory).filePath(QStringLiteral("addons")), this);
            msg_Dbg(m_intf, "Add-on store directory: %s", qtu(addonManager->storageDirectory()));
            // Several app windows share this profile. Attach the profile peer
            // before the bundled setup and before any native consumer starts.
            auto* profileHost = new vlc::addons::AddonProfileHost(addonManager,
                vlc::addons::AddonStreamLifetime::applicationHandle(), this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, profileHost, [profileHost] { profileHost->close(); },
                Qt::DirectConnection);
            if (!profileHost->start())
                msg_Warn(m_intf, "Cannot share the add-on profile with other app windows");
            qmlRegisterSingletonInstance<vlc::addons::AddonManager>(uri, versionMajor, versionMinor, "AddonManager", addonManager);
            addonManager->setMediaCapabilitiesSource(m_intf->p_mainPlayerController);
            // File and folder settings use the app's own dialog; the add-on
            // gets a handle, not the path.
            addonManager->setPathPicker([](const QString& kind, std::function<void(const QString&)> done) {
                auto* dialog = new QFileDialog(nullptr, kind == QLatin1String("folder") ? qtr("Select a folder") : qtr("Select a file"));
                dialog->setAttribute(Qt::WA_DeleteOnClose);
                dialog->setFileMode(kind == QLatin1String("folder") ? QFileDialog::Directory : QFileDialog::ExistingFile);
                if (kind == QLatin1String("folder")) dialog->setOption(QFileDialog::ShowDirsOnly);
                auto answered = std::make_shared<bool>(false);
                QObject::connect(dialog, &QFileDialog::fileSelected, dialog, [done, answered](const QString& path) { *answered = true; done(path); });
                QObject::connect(dialog, &QDialog::finished, dialog, [done, answered] { if (!*answered) done({}); });
                dialog->open();
            });
            // Read-only queue, library and playing-file projections for add-ons.
            mountAddonGuestSources(m_intf, m_mainCtx, addonManager->hostServices(), this);
            // The other episodes of the playing media, for add-on player panels.
            auto* series = new SeriesModel(this); series->setObjectName(QStringLiteral("redesignSeriesModel"));
            mountAddonMediaSiblings(addonManager->hostServices(), series, this)->setObjectName(QStringLiteral("redesignMediaSiblings"));
            char* sharedDirectory = config_GetSysPath(VLC_PKG_DATA_DIR, nullptr);
            const auto installedRoot = sharedDirectory ? QDir(qfu(sharedDirectory)).filePath(QStringLiteral("addons/reference")) : QString();
            free(sharedDirectory);
            QList<vlc::addons::DistributionBootstrapEntry> bundledAddons;
            // The artwork and Radio Browser examples are not bundled. The Lua
            // art scripts serve artwork until the native metadata owner is on.
            // Profiles that already have these add-ons keep them.
            for (const auto& entry : QList<QPair<QString,QString>>{
                    {QStringLiteral("tvmaze"),QStringLiteral("org.videolan.tvmaze")},
                    {QStringLiteral("thesubtitledb"),QStringLiteral("org.hakone.thesubtitledb")},
                    {QStringLiteral("episodes"),QStringLiteral("org.videolan.episodes")}}) {
                bundledAddons.append({entry.second,
                    installedRoot.isEmpty()?QString():QDir(installedRoot).filePath(entry.first),
                    QDir(QStringLiteral(VLC_ADDON_REFERENCE_BUILD_ROOT)).filePath(entry.first)});
            }
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, addonManager,
                &vlc::addons::AddonManager::cancelDistributionBootstrap, Qt::DirectConnection);
            connect(addonManager, &vlc::addons::AddonManager::distributionBootstrapChanged, this,
                [this, owner=QPointer<vlc::addons::AddonManager>(addonManager)] {
                    if(!owner || owner->distributionBootstrapInProgress())return;
                    for(const auto& result:owner->distributionBootstrapResults()) {
                        const auto row=result.toMap();if(row.value(QStringLiteral("success")).toBool())continue;
                        const auto id=row.value(QStringLiteral("id")).toString(), error=row.value(QStringLiteral("error")).toString();
                        msg_Warn(m_intf,"Cannot prepare bundled add-on %s: %s",qtu(id),qtu(error));
                    }
                });
            if(!addonManager->beginDistributionBootstrap(std::move(bundledAddons)))
                msg_Warn(m_intf,"Cannot start bundled add-on preparation: %s",qtu(addonManager->errorMessage()));
            auto* displayCatalogue = new vlc::addons::AddonCatalogueView(addonManager,
                m_mainCtx->addonStreamLifetime());
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, displayCatalogue,
                &vlc::addons::AddonCatalogueView::close, Qt::DirectConnection);
            auto* packageSummaries = new vlc::addons::AddonPackageSummaries(addonManager,
                m_mainCtx->addonStreamLifetime(), this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, packageSummaries,
                &vlc::addons::AddonPackageSummaries::close, Qt::DirectConnection);
            qmlRegisterSingletonInstance<vlc::addons::AddonPackageSummaries>(uri, versionMajor, versionMinor,
                "AddonPackageSummaries", packageSummaries);
            metadataService = new AddonMetadataService(addonManager, this);
            if (!metadataService->bindApplicationLifetime(m_mainCtx->addonStreamLifetime()))
                metadataService->shutdown();
            connect(m_mainCtx, &MainCtx::addonWorkRetiring,
                metadataService, &AddonMetadataService::shutdown, Qt::DirectConnection);
            connect(addonManager, &vlc::addons::AddonManager::retiring,
                metadataService, &AddonMetadataService::shutdown, Qt::DirectConnection);
            metadataService->setAutomaticNetworkAllowed(var_InheritBool(m_intf, "metadata-network-access"));
            m_intf->p_mainPlayerController->setMetadataService(metadataService);
            connect(m_intf->p_mainPlayerController, &PlayerController::metadataNetworkAccessChanged,
                metadataService, &AddonMetadataService::setAutomaticNetworkAllowed);
            // For the Windows cursor check: Tools > Messages, debug level.
            vlc::addons::setHelperLaunchObserver([intf = m_intf](quint64 count, const QString& program) {
                msg_Dbg(intf, "Add-on helper launch %llu%s%s", static_cast<unsigned long long>(count),
                        program.isEmpty() ? "" : ": ", qtu(program));
            });
            auto acceptedIdentities = std::make_shared<vlc::addons::AddonAcceptedIdentities>(addonManager->storageDirectory());
            metadataService->setAcceptedIdentities(acceptedIdentities);
            auto* identityChoice = new vlc::addons::AddonIdentityChoice(metadataService, acceptedIdentities,
                [player = m_intf->p_mainPlayerController->getPlayer()]() -> input_item_t* {
                    vlc_player_locker lock(player);
                    input_item_t* item = vlc_player_GetCurrentMedia(player);
                    return item ? input_item_Hold(item) : nullptr;
                }, this);
            connect(m_intf->p_mainPlayerController, &PlayerController::inputChanged,
                identityChoice, &vlc::addons::AddonIdentityChoice::cancel);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring,
                identityChoice, &vlc::addons::AddonIdentityChoice::close, Qt::DirectConnection);
            qmlRegisterSingletonInstance<vlc::addons::AddonIdentityChoice>(uri, versionMajor, versionMinor,
                "AddonIdentityChoice", identityChoice);
            // One-way for this instance: once the owner is registered, Core never
            // runs its own online lookups again, even after the owner closes.
            if (var_InheritBool(m_intf, "metadata-native-owner")) {
                AddonNativeMetadataOwner::Options options;
                options.cacheDirectory = QDir(addonManager->storageDirectory()).filePath(QStringLiteral("native-artwork-cache"));
                options.activateCoreOwner = true;
                // Every family's add-on passed the parity test, so no Core
                // script runs under the owner.
                options.nativeFamilies = QStringList{};
                m_metadataOwner = AddonNativeMetadataOwner::create(metadataService, addonManager, VLC_OBJECT(m_intf), options);
                if (!m_metadataOwner)
                    msg_Warn(m_intf, "Cannot start the add-on metadata owner; online metadata lookups are off");
                else
                    msg_Dbg(m_intf, "Add-on metadata owner active");
            }
            auto* themes = new vlc::addons::AddonThemeService(addonManager, this);
            const auto updateThemeAccent = [this, themes] {
                char* option = var_InheritString(m_intf, "qt-accent-color");
                themes->setUserAccentOverride(option ? qfu(option) : QString());
                free(option);
            };
            updateThemeAccent();
            themes->observePlatformPreferences(qApp->styleHints());
            connect(m_mainCtx, &MainCtx::redesignAccentChanged, themes, updateThemeAccent);
            connect(m_mainCtx, &MainCtx::redesignPlayerPreferencesChanged, themes, updateThemeAccent);
            qmlRegisterSingletonInstance<vlc::addons::AddonThemeService>(uri, versionMajor, versionMinor, "AddonThemes", themes);
            auto* addonUi = new vlc::addons::AddonUiRegistry(addonManager, this);
            auto* uiAssets = new vlc::addons::AddonUiAssets(addonManager, this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, uiAssets, &vlc::addons::AddonUiAssets::close);
            addonUi->setAssetService(uiAssets);
            qmlRegisterSingletonInstance<vlc::addons::AddonUiRegistry>(uri, versionMajor, versionMinor, "AddonUiRegistry", addonUi);
            auto* visualizers = vlc::addons::createPlayerVisualizers(addonManager, m_intf->p_mainPlayerController, this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, visualizers, &vlc::addons::AddonVisualizers::close);
            qmlRegisterSingletonInstance<vlc::addons::AddonVisualizers>(uri, versionMajor, versionMinor, "AddonVisualizers", visualizers);
            auto* sessions = new vlc::addons::AddonPlayerSessions(addonManager,
                m_intf->p_mainPlayerController->getPlayer(), m_intf->p_mainPlayerController,
                m_mainCtx->addonStreamLifetime(), this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring,
                sessions, &vlc::addons::AddonPlayerSessions::close, Qt::DirectConnection);
            sessions->activate();
            if (sessions->ui())
                qmlRegisterSingletonInstance<vlc::addons::AddonSessionUi>(uri, versionMajor, versionMinor, "AddonSessions", sessions->ui());
            const QPointer<vlc::addons::AddonManager> managerOwner(addonManager);
            auto admissions = std::make_shared<vlc::addons::AddonSourceAdmissions>(addonManager->storageDirectory(),
                [managerOwner](const QVariantMap& reference) { return vlc::addons::discoveryReferenceAvailable(managerOwner, reference); });
            auto* nativeQueue = new vlc::addons::AddonNativeQueueStore(m_intf->p_mainPlaylistController ? m_intf->p_mainPlaylistController->getPlaylist().m_playlist : nullptr, admissions, this);
            connect(m_mainCtx, &MainCtx::addonWorkRetiring,
                nativeQueue, &vlc::addons::AddonNativeQueueStore::close, Qt::DirectConnection);
            connect(addonManager, &vlc::addons::AddonManager::retiring,
                nativeQueue, &vlc::addons::AddonNativeQueueStore::close, Qt::DirectConnection);
            nativeQueue->setObjectName(QStringLiteral("addonNativeQueueStore"));
            auto* sourceOwner = new vlc::addons::AddonDiscoverySourceOwner(addonManager,
                VLC_OBJECT(m_intf), admissions, m_mainCtx->addonStreamLifetime(), this);
            const QPointer<vlc::addons::AddonDiscoverySourceOwner> sourceAccess(sourceOwner);
            const QPointer<vlc::addons::AddonManager> sourceManager(addonManager);
            connect(sourceOwner, &vlc::addons::AddonDiscoverySourceOwner::changed, addonManager,
                [sourceAccess, sourceManager] {
                    if (sourceManager) sourceManager->setNativeStreamAvailable(sourceAccess && sourceAccess->active());
                    if (sourceManager) sourceManager->setNativeStreamTransportHint(
                        sourceAccess && sourceAccess->transportKnown(),
                        sourceAccess && sourceAccess->transportAvailable());
                });
            connect(m_mainCtx, &MainCtx::addonWorkRetiring, sourceOwner,
                [sourceAccess, sourceManager] {
                    if (sourceManager) sourceManager->setNativeStreamAvailable(false);
                    if (sourceManager) sourceManager->setNativeStreamTransportHint(false, false);
                    if (sourceAccess) sourceAccess->close();
                }, Qt::DirectConnection);
            connect(m_intf->p_mainPlayerController, &QObject::destroyed, sourceOwner,
                [sourceAccess, sourceManager] {
                    if (sourceManager) sourceManager->setNativeStreamAvailable(false);
                    if (sourceManager) sourceManager->setNativeStreamTransportHint(false, false);
                    if (sourceAccess) sourceAccess->close();
                });
            sourceOwner->activate();
            auto nativeBackend=nativeQueue->backend();
            vlc::addons::AddonNativeLibraryTargets* libraryTargets=nullptr;
            if (auto* mediaLibrary=m_mainCtx->getMediaLibrary()) {
                const auto dispatcher=vlc::addons::AddonNativeLibraryStore::dispatcherFor(mediaLibrary);
                auto* libraryStore=new vlc::addons::AddonNativeLibraryStore(dispatcher,admissions,addonManager,this);
                connect(m_mainCtx,&MainCtx::addonWorkRetiring,libraryStore,&vlc::addons::AddonNativeLibraryStore::close);
                libraryStore->setObjectName(QStringLiteral("addonNativeLibraryStore"));
                libraryTargets=new vlc::addons::AddonNativeLibraryTargets(dispatcher,libraryStore,this);
                connect(m_mainCtx,&MainCtx::addonWorkRetiring,libraryTargets,&vlc::addons::AddonNativeLibraryTargets::close);
                connect(addonManager,&QObject::destroyed,libraryTargets,&vlc::addons::AddonNativeLibraryTargets::close);
                nativeBackend=vlc::addons::combineDiscoveryBackends(std::move(nativeBackend),libraryStore->backend());
            }
            auto* discovery = vlc::addons::createPlayerDiscoveryWithSources(addonManager, sourceOwner, std::move(nativeBackend), this);
            if (libraryTargets) discovery->setPlaylistBackend(libraryTargets->backend());
            // A queue-fill session reacts to queue changes only.
            connect(nativeQueue, &vlc::addons::AddonNativeQueueStore::revisionChanged, discovery, &vlc::addons::AddonDiscoveryService::noteQueueChanged);
            qmlRegisterSingletonInstance<vlc::addons::AddonDiscoveryService>(uri, versionMajor, versionMinor, "AddonDiscovery", discovery);
#ifdef VLC_ADDON_TEST_DRIVER
            if (auto* driver = vlc::addons::AddonTestDriver::create(addonManager, packageSummaries, discovery, this)) {
                connect(m_mainCtx, &MainCtx::addonWorkRetiring, driver, [driver] { driver->close(); }, Qt::DirectConnection);
                driver->setMetadataReport([this, service = QPointer<AddonMetadataService>(metadataService)] {
                    bool owned = false;
                    vlc_metadata_backend_t* current = vlc_metadata_owner_Hold(VLC_OBJECT(m_intf), &owned);
                    vlc_metadata_backend_t* ours = service ? service->holdCoreBackend() : nullptr;
                    const bool same = current && current == ours;
                    if (current) vlc_metadata_backend_Release(current);
                    if (ours) vlc_metadata_backend_Release(ours);
                    return QVariantMap{{QStringLiteral("owned"), owned}, {QStringLiteral("ownerIsThisWindow"), same},
                        {QStringLiteral("ownerActive"), m_metadataOwner && m_metadataOwner->active()},
                        {QStringLiteral("coreRequests"), service ? qint64(service->coreRequestCount()) : -1}};
                });
                // What the player gets: the registry's panels, as QML reads them.
                driver->setPanelsReport([registry = QPointer<vlc::addons::AddonUiRegistry>(findChild<vlc::addons::AddonUiRegistry*>()),
                                         series = QPointer<SeriesModel>(findChild<SeriesModel*>(QStringLiteral("redesignSeriesModel")))](const QString& argument) {
                    if (!registry) return QVariantMap{{QStringLiteral("registry"), false}};
                    const auto verb = argument.section(QLatin1Char(' '), 0, 0), value = argument.section(QLatin1Char(' '), 1);
                    if (verb == QLatin1String("open")) {
                        // As the player's panel frame opens one: a session,
                        // its owner, then the panel's media context.
                        const auto session = registry->createSession(value, {}, {{QStringLiteral("surface"), QStringLiteral("player.panels@1")}});
                        if (session.isEmpty()) return QVariantMap{{QStringLiteral("session"), QString()}};
                        auto* owner = new QObject(registry);
                        owner->setObjectName(QStringLiteral("driverPanel:") + session);
                        connect(registry, &vlc::addons::AddonUiRegistry::sessionFailed, owner, [owner, session](const QString& id) {
                            if (id == session) owner->setProperty("failed", true);
                        });
                        const bool bound = registry->bindSessionOwner(session, owner);
                        return QVariantMap{{QStringLiteral("session"), session}, {QStringLiteral("bound"), bound},
                            {QStringLiteral("attached"), registry->attachPanelSession(session)}};
                    }
                    if (verb == QLatin1String("state")) {
                        auto* owner = registry->findChild<QObject*>(QStringLiteral("driverPanel:") + value);
                        return QVariantMap{{QStringLiteral("descriptor"), registry->session(value)},
                            {QStringLiteral("failed"), owner && owner->property("failed").toBool()}};
                    }
                    QVariantMap result{{QStringLiteral("registry"), true}, {QStringLiteral("available"), registry->available()},
                        {QStringLiteral("siblingsWanted"), registry->siblingsWanted()}, {QStringLiteral("panels"), registry->panels()}};
                    if (series)
                        result.insert(QStringLiteral("series"), QVariantMap{{QStringLiteral("available"), series->available()},
                            {QStringLiteral("loading"), series->loading()}, {QStringLiteral("siblings"), qint64(series->siblings().size())}});
                    return result;
                });
            }
#endif
        }
        qmlRegisterType<vlc::addons::AddonVisualizerSurface>(uri, versionMajor, versionMinor, "AddonVisualizerSurface");
        auto* subtitleSources = new SubtitleSourceService(this);
        subtitleSources->setAddonManager(addonManager);
        subtitleSources->setMetadataService(metadataService);
        if (!dataDirectory.isEmpty()) subtitleSources->setStorageDirectory(QDir(dataDirectory).filePath(QStringLiteral("subtitles")));
        qmlRegisterSingletonInstance<SubtitleSourceService>(uri, versionMajor, versionMinor, "SubtitleSources", subtitleSources);
        qmlRegisterType<PlayerHighResolutionTimeUpdater>(uri, versionMajor, versionMinor, "HighResolutionTimeUpdater");
        qmlRegisterUncreatableMetaObject(VLCTickForeign::staticMetaObject, uri, versionMajor, versionMinor, "VLCTick", "Not Instantiable" );
        qmlRegisterTypesAndRevisions<VLCDuration>(uri, versionMajor);
        qmlRegisterTypesAndRevisions<VLCTime>(uri, versionMajor);

        qmlRegisterType<QmlBookmarkMenu>( uri, versionMajor, versionMinor, "QmlBookmarkMenu" );
        qmlRegisterType<QmlProgramMenu>( uri, versionMajor, versionMinor, "QmlProgramMenu" );
        qmlRegisterType<QmlRendererMenu>( uri, versionMajor, versionMinor, "QmlRendererMenu" );
        qmlRegisterType<QmlSubtitleMenu>( uri, versionMajor, versionMinor, "QmlSubtitleMenu" );
        qmlRegisterType<QmlAudioMenu>( uri, versionMajor, versionMinor, "QmlAudioMenu" );
        qmlRegisterType<QmlAudioContextMenu>( uri, versionMajor, versionMinor, "QmlAudioContextMenu" );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.PlayerControls";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.PlayerControls
        qmlRegisterUncreatableType<ControlbarProfileModel>(uri, versionMajor, versionMinor, "ControlbarProfileModel", "");
        qmlRegisterUncreatableType<ControlbarProfile>(uri, versionMajor, versionMinor, "ControlbarProfile", "");
        qmlRegisterUncreatableType<PlayerControlbarModel>(uri, versionMajor, versionMinor, "PlayerControlbarModel", "");
        qmlRegisterUncreatableType<ControlListModel>( uri, versionMajor, versionMinor, "ControlListModel", "" );
        qmlRegisterType<ControlListFilter>(uri, versionMajor, versionMinor, "ControlListFilter");
        qmlRegisterSingletonType(uri, versionMajor, versionMinor, "PlayerListModel", PlayerControlbarModel::getPlaylistIdentifierListModel);


        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Playlist";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Playlist
        qmlRegisterUncreatableType<PlaylistItem>(uri, versionMajor, versionMinor, "playlistItem", "");
        qmlRegisterType<PlaylistListModel>( uri, versionMajor, versionMinor, "PlaylistListModel" );
        qmlRegisterType<PlaylistTreeModel>( uri, versionMajor, versionMinor, "PlaylistTreeModel" );
        qmlRegisterType<PlaylistController>( uri, versionMajor, versionMinor, "PlaylistController" );
        qmlRegisterType<PlaylistContextMenu>( uri, versionMajor, versionMinor, "PlaylistContextMenu" );
        assert(m_intf->p_mainPlaylistController);
        qmlRegisterSingletonInstance<PlaylistController>(uri, versionMajor, versionMinor, "MainPlaylistController", m_intf->p_mainPlaylistController);

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Network";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Network
        qmlRegisterType<NetworkPlaylistModel>(uri, versionMajor, versionMinor, "NetworkPlaylistModel");
        qmlRegisterType<NetworkMediaModel>( uri, versionMajor, versionMinor, "NetworkMediaModel");
        qmlRegisterType<NetworkDeviceModel>( uri, versionMajor, versionMinor, "NetworkDeviceModel");
        qmlRegisterType<NetworkSourcesModel>( uri, versionMajor, versionMinor, "NetworkSourcesModel");
        qmlRegisterType<AddonsModel>( uri, versionMajor, versionMinor, "ServicesDiscoveryModel");
        qmlRegisterType<StandardPathModel>( uri, versionMajor, versionMinor, "StandardPathModel");
        qmlRegisterType<MLFoldersModel>( uri, versionMajor, versionMinor, "MLFolderModel");

        qmlRegisterType<NetworkMediaContextMenu>( uri, versionMajor, versionMinor, "NetworkMediaContextMenu" );
        qmlRegisterType<NetworkDeviceContextMenu>( uri, versionMajor, versionMinor, "NetworkDeviceContextMenu" );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Style";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Style
        qmlRegisterUncreatableType<ColorSchemeModel>(uri, versionMajor, versionMinor, "ColorSchemeModel", "");
        qmlRegisterType<ColorContext>(uri, versionMajor, versionMinor, "ColorContext");
        qmlRegisterUncreatableType<ColorProperty>(uri, versionMajor, versionMinor, "colorProperty", "");
        qmlRegisterType<SystemPalette>(uri, versionMajor, versionMinor, "SystemPalette");
        qmlRegisterTypesAndRevisions<VLCIcons>( uri, versionMajor );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Util";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Util
        qmlRegisterSingletonInstance<QmlKeyHelper>(uri, versionMajor, versionMinor, "KeyHelper", new QmlKeyHelper(this));
        qmlRegisterSingletonType<EffectsImageProvider>(uri, versionMajor, versionMinor, "Effects", EffectsImageProvider::instance);
        qmlRegisterUncreatableType<SVGColorImageBuilder>(uri, versionMajor, versionMinor, "SVGColorImageBuilder", "");
        qmlRegisterSingletonInstance<SVGColorImage>(uri, versionMajor, versionMinor, "SVGColorImage", new SVGColorImage(this));
        qmlRegisterSingletonInstance<VLCAccessImage>(uri, versionMajor, versionMinor, "VLCAccessImage", new VLCAccessImage(this));
        qmlRegisterType<DelayEstimator>( uri, versionMajor, versionMinor, "DelayEstimator" );
        qmlRegisterTypesAndRevisions<WheelToVLCConverter>( uri, versionMajor );

        qmlRegisterType<ImageLuminanceExtractor>( uri, versionMajor, versionMinor, "ImageLuminanceExtractor");

        qmlRegisterType<ItemKeyEventFilter>( uri, versionMajor, versionMinor, "KeyEventFilter" );

        // WARNING: This type is deprecated, do not use it.
        qmlRegisterType<FlickableScrollHandler>( uri, versionMajor, versionMinor, "VLCFlickableScrollHandler" );

        {
            auto funcRegisterScrollHandler = qmlRegisterType<DummyFlickableScrollHandler>;
            if (var_InheritBool(m_intf, "qt-use-scroll-handler"))
                funcRegisterScrollHandler = qmlRegisterType<Kirigami::WheelHandler>;

            funcRegisterScrollHandler( uri, versionMajor, versionMinor, "FlickableScrollHandler" );
        }

        qmlRegisterType<ListSelectionModel>( uri, versionMajor, versionMinor, "ListSelectionModel" );
        qmlRegisterType<DoubleClickIgnoringItem>( uri, versionMajor, versionMinor, "DoubleClickIgnoringItem" );
        qmlRegisterType<TextureProviderObserver>( uri, versionMajor, versionMinor, "TextureProviderObserver" );
        qmlRegisterType<TextureProviderIndirection>( uri, versionMajor, versionMinor, "TextureProviderIndirection" );
        qmlRegisterType<SGManipulator>( uri, versionMajor, versionMinor, "SGManipulator" );
        qmlRegisterTypesAndRevisions<AccessibleCompatAttached>( uri, versionMajor );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    {
        const char* uri = "VLC.Widgets";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.Widgets
        qmlRegisterType<SoftwareBackdropBlur>(uri, versionMajor, versionMinor, "SoftwareBackdropBlur");
        qmlRegisterType<CSDThemeImage>(uri, versionMajor, versionMinor, "CSDThemeImage");
        qmlRegisterType<ViewBlockingRectangle>( uri, versionMajor, versionMinor, "ViewBlockingRectangle" );
        qmlRegisterTypesAndRevisions<PointingToolTipAttached>( uri, versionMajor );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    if (m_mainCtx->hasMediaLibrary())
    {
        const char* uri = "VLC.MediaLibrary";
        const int versionMajor = 1;
        const int versionMinor = 0;

        // @uri VLC.MediaLibrary
        assert(m_mainCtx->getMediaLibrary());
        qmlRegisterSingletonInstance<MediaLib>(uri, versionMajor, versionMinor, "MediaLib", m_mainCtx->getMediaLibrary());

        qmlRegisterUncreatableType<MLItemId>( uri, versionMajor, versionMinor, "mediaId", "");
        qmlRegisterUncreatableType<MLBaseModel>( uri, versionMajor, versionMinor, "MLBaseModel", "ML Base Model is uncreatable." );
        qmlRegisterType<MLAlbumModel>( uri, versionMajor, versionMinor, "MLAlbumModel" );
        qmlRegisterType<MLArtistModel>( uri, versionMajor, versionMinor, "MLArtistModel" );
        qmlRegisterType<MLAudioModel>( uri, versionMajor, versionMinor, "MLAudioModel" );
        qmlRegisterType<MLGenreModel>( uri, versionMajor, versionMinor, "MLGenreModel" );
        qmlRegisterType<MLUrlModel>( uri, versionMajor, versionMinor, "MLUrlModel" );
        qmlRegisterType<MLMediaModel>( uri, versionMajor, versionMinor, "MLMediaModel" );
        qmlRegisterType<MLFileArtwork>( uri, versionMajor, versionMinor, "MLFileArtwork" );
        qmlRegisterType<MLVideoModel>( uri, versionMajor, versionMinor, "MLVideoModel" );
        qmlRegisterType<MLRecentAudioModel>( uri, versionMajor, versionMinor, "MLRecentAudioModel" );
        qmlRegisterType<MLPlayedArtistModel>( uri, versionMajor, versionMinor, "MLPlayedArtistModel" );
        qmlRegisterType<MLRecentVideoModel>( uri, versionMajor, versionMinor, "MLRecentVideoModel" );
        qmlRegisterType<MLVideoGroupsModel>( uri, versionMajor, versionMinor, "MLVideoGroupsModel" );
        qmlRegisterType<MLVideoFoldersModel>( uri, versionMajor, versionMinor, "MLVideoFoldersModel" );
        qmlRegisterType<MLPlaylistListModel>( uri, versionMajor, versionMinor, "MLPlaylistListModel" );
        qmlRegisterType<MLPlaylistModel>( uri, versionMajor, versionMinor, "MLPlaylistModel" );
        qmlRegisterType<MLBookmarkModel>( uri, versionMajor, versionMinor, "MLBookmarkModel" );

        qmlRegisterType<NetworkMediaModel>( uri, versionMajor, versionMinor, "NetworkMediaModel");
        qmlRegisterType<NetworkDeviceModel>( uri, versionMajor, versionMinor, "NetworkDeviceModel");
        qmlRegisterType<NetworkSourcesModel>( uri, versionMajor, versionMinor, "NetworkSourcesModel");
        qmlRegisterType<AddonsModel>( uri, versionMajor, versionMinor, "ServicesDiscoveryModel");
        qmlRegisterType<MLFoldersModel>( uri, versionMajor, versionMinor, "MLFolderModel");
        qmlRegisterType<MLRecentMediaModel>( uri, versionMajor, versionMinor, "MLRecentMediaModel" );

        qmlRegisterType<PlaylistListContextMenu>( uri, versionMajor, versionMinor, "PlaylistListContextMenu" );
        qmlRegisterType<PlaylistMediaContextMenu>( uri, versionMajor, versionMinor, "PlaylistMediaContextMenu" );

        qmlRegisterModule(uri, versionMajor, versionMinor);
        qmlProtectModule(uri, versionMajor);
    }

    g_qmlTypesAreRegistered = true;
}

void MainUI::clearQMLTypes()
{
    qmlClearTypeRegistrations();
    g_qmlTypesAreRegistered = false;
}
