/*****************************************************************************
 * Copyright (C) 2024 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/
#include "vlc_stub_modules.hpp"

#include <QTest>
#include <QSignalSpy>
#include <QThreadPool>
#include <QGuiApplication>
#include <QClipboard>
#include <QScopeGuard>
#include <QtPlugin>
#include <thread>
#if defined(QT_STATIC) && defined(Q_OS_WIN)
Q_IMPORT_PLUGIN(QWindowsIntegrationPlugin)
#endif
#include <vlc_variables.h>
#include "dialogs/dialogs/dialogmodel.hpp"
#include <vlc_cxx_helpers.hpp>

#include "../util/renderer_manager.hpp"

class TestVLCDialogModel : public QObject
{
    Q_OBJECT

    enum UserAnswer {
        Dismiss,
        PostLogin,
        PostAction1,
        PostAction2
    };

private slots:
    void initTestCase() {
        m_env = std::make_unique<VLCTestingEnv>();
        QVERIFY(m_env->init());
        m_intf = m_env->intf;
    }

    void cleanupTestCase() {
        m_intf = nullptr;
        m_env.reset();
    }

    void init() {
        var_Create(m_intf, "qt-error-dialogs", VLC_VAR_BOOL);
        var_SetBool(m_intf, "qt-error-dialogs", true);
        var_Create(m_intf, "qt-minor-warnings", VLC_VAR_BOOL);
        var_SetBool(m_intf, "qt-minor-warnings", false);
        VLCDialogModel::getInstance(m_intf);
        m_dialog = new VLCDialog();
        m_loginSpy = std::make_unique<QSignalSpy>(m_dialog, &VLCDialog::login);
        m_questionSpy = std::make_unique<QSignalSpy>(m_dialog, &VLCDialog::question);
        m_progressSpy = std::make_unique<QSignalSpy>(m_dialog, &VLCDialog::progress);
        m_progressUpdatedSpy = std::make_unique<QSignalSpy>(m_dialog, &VLCDialog::progressUpdated);
        m_canceledSpy = std::make_unique<QSignalSpy>(m_dialog, &VLCDialog::cancelled);
    }

    void cleanup() {
        m_loginSpy.reset();
        m_questionSpy.reset();
        m_progressSpy.reset();
        m_progressUpdatedSpy.reset();
        m_canceledSpy.reset();
        VLCDialogModel* model = VLCDialogModel::getInstance<false>();
        if (model) {
            model->setProvider(nullptr);
            VLCDialogModel::killInstance();
        }
        delete m_dialog;
        var_Destroy(m_intf, "qt-error-dialogs");
        var_Destroy(m_intf, "qt-minor-warnings");
    }

    void testLogin_data() {
        QTest::addColumn<UserAnswer>("userReply");
        QTest::addColumn<int>("expectedReply");
        QTest::addColumn<bool>("notificationsEnabled");

        QTest::newRow("login") << PostLogin << 1 << true;
        QTest::newRow("dismiss") << Dismiss << 0 << true;
        QTest::newRow("wrongAnswer") << PostAction1 << VLC_EGENERIC << true;
        QTest::newRow("loginWithNotificationsOff") << PostLogin << 1 << false;
    }

    //failed state when no renderer manager is found
    void testLogin() {
        QFETCH(UserAnswer, userReply);
        QFETCH(int, expectedReply);
        QFETCH(bool, notificationsEnabled);
        var_SetBool(m_intf, "qt-error-dialogs", notificationsEnabled);

        QObject::connect(m_dialog, &VLCDialog::login, this, [&](
            DialogId dialogId, const QString & title,
            const QString & text, const QString & defaultUsername,
            bool){

            QCOMPARE(defaultUsername, "default username");
            QCOMPARE(title, "login title");
            QCOMPARE(text, "login message");

            switch(userReply)
            {
            case Dismiss:
                m_dialog->dismiss(dialogId);
                break;
            case PostLogin:
                m_dialog->post_login(dialogId, "username", "hunter2", false);
                break;
            case PostAction1:
            default:
                //bad behavior
                m_dialog->post_action1(dialogId);
                break;

            }
        });

        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);

        std::atomic<bool> replied = false;
        int reply = -1;
        QString repliedUsername;
        QString repliedPassword;
        bool repliedStore = true;
        QThreadPool::globalInstance()->start([&](){
            char* username = nullptr;
            char* password = nullptr;
            reply = vlc_dialog_wait_login(m_intf, &username, &password, &repliedStore,
                                  "default username", "login title",
                                  "login message");
            if (reply == 1) {
                repliedUsername = username;
                repliedPassword = password;
                free(username);
                free(password);
            }

            replied = true;
        });

        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        if (expectedReply == VLC_EGENERIC)
            QCOMPARE_LT(reply, 0);
        else
            QCOMPARE(reply, expectedReply);

        if (reply == 1) {
            QCOMPARE(repliedUsername, "username");
            QCOMPARE(repliedPassword, "hunter2");
            QCOMPARE(repliedStore, false);
        }

        QCOMPARE(m_questionSpy->count(), 0);
        QCOMPARE(m_progressSpy->count(), 0);
        QCOMPARE(m_progressUpdatedSpy->count(), 0);
        QCOMPARE(m_canceledSpy->count(), 0);
    }

    void testSuppressedQuestions_data() {
        QTest::addColumn<int>("type");
        QTest::newRow("normal") << int(VLC_DIALOG_QUESTION_NORMAL);
        QTest::newRow("warning") << int(VLC_DIALOG_QUESTION_WARNING);
    }

    void testSuppressedQuestions() {
        QFETCH(int, type);
        var_SetBool(m_intf, "qt-error-dialogs", false);
        // No provider is installed: suppression must return without waiting
        // for the UI or accepting either action.
        int reply = vlc_dialog_wait_question(m_intf, vlc_dialog_question_type(type),
            "Cancel", "Continue", "Other", "Question", "Test question");
        QCOMPARE(reply, 0);
        QCOMPARE(m_questionSpy->count(), 0);
    }

    void testCriticalQuestionWithNotificationsOff() {
        var_SetBool(m_intf, "qt-error-dialogs", false);
        QObject::connect(m_dialog, &VLCDialog::question, this,
            [this](DialogId id, const QString &, const QString &, VLCDialog::QuestionType type,
                   const QString &, const QString &, const QString &) {
                QCOMPARE(type, VLCDialog::QUESTION_CRITICAL);
                m_dialog->post_action1(id);
            });
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);
        int reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_CRITICAL,
            "Cancel", "Continue", nullptr, "Critical question", "Test question");
        QCOMPARE(reply, 1);
        QCOMPARE(m_questionSpy->count(), 1);
    }

    void testWarningsOffByDefault() {
        DialogErrorModel errors(m_intf);
        QSignalSpy notices(&errors, &DialogErrorModel::notificationRequested);
        msg_Warn(m_intf, "Hidden minor warning");
        QTest::qWait(300);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(notices.count(), 0);
        QVERIFY(!errors.warningNotificationsEnabled());
    }

    void testWarningBatchAndIndependentPreference() {
        var_SetBool(m_intf, "qt-minor-warnings", true);
        var_SetBool(m_intf, "qt-error-dialogs", false);
        DialogErrorModel errors(m_intf);
        QSignalSpy notices(&errors, &DialogErrorModel::notificationRequested);
        for (int i = 0; i < 3000; ++i) msg_Warn(m_intf, "Late frame %d", i);
        QCOMPARE(errors.count(), 0);
        QTRY_COMPARE(notices.count(), 1);
        QCOMPARE(errors.count(), 1);
        QCOMPARE(errors.repeatedMessageCount(), 3000);
        QVERIFY(errors.notificationIsWarning());
        QVERIFY(errors.notificationAllowed());
        QVERIFY(errors.data(errors.index(0), DialogErrorModel::DIALOG_TEXT).toString().contains("3000 warnings"));
        errors.refreshNotificationPreference();
        QCOMPARE(errors.repeatedMessageCount(), 3000); // Error toggle must not dismiss a warning.
        var_SetBool(m_intf, "qt-minor-warnings", false);
        errors.refreshNotificationPreference();
        QCOMPARE(errors.repeatedMessageCount(), 0);
        QVERIFY(!errors.notificationAllowed());
        msg_Warn(m_intf, "Disabled warning");
        QTest::qWait(300);
        QCOMPARE(notices.count(), 1);
    }

    void testWarningPriorityAndHistory() {
        var_SetBool(m_intf, "qt-minor-warnings", true);
        DialogErrorModel errors(m_intf);
        QSignalSpy notices(&errors, &DialogErrorModel::notificationRequested);
        errors.pushError("Cannot open media", "Error details");
        QCOMPARE(notices.count(), 1);
        for (int i = 0; i < 205; ++i) {
            msg_Warn(m_intf, "Warning %d", i);
            QVERIFY(QMetaObject::invokeMethod(&errors, "flushWarnings"));
        }
        QCOMPARE(errors.count(), 201);
        QCOMPARE(errors.notificationText(), "Cannot open media");
        QVERIFY(!errors.notificationIsWarning());
        QCOMPARE(notices.count(), 1);
        QCOMPARE(errors.data(errors.index(0), DialogErrorModel::DIALOG_TEXT).toString(), "Error details");
        QVERIFY(errors.data(errors.index(1), DialogErrorModel::DIALOG_TEXT).toString().contains("Warning 5"));
        errors.resetRepeatedMessageCount();
        msg_Warn(m_intf, "New notice");
        QVERIFY(QMetaObject::invokeMethod(&errors, "flushWarnings"));
        QCOMPARE(notices.count(), 2);
        QVERIFY(errors.notificationIsWarning());
    }

    void testWarningDisableDropsPendingBatch() {
        DialogErrorModel errors(m_intf);
        var_SetBool(m_intf, "qt-minor-warnings", true);
        errors.refreshNotificationPreference();
        msg_Warn(m_intf, "Pending warning");
        var_SetBool(m_intf, "qt-minor-warnings", false);
        errors.refreshNotificationPreference();
        var_SetBool(m_intf, "qt-minor-warnings", true);
        errors.refreshNotificationPreference();
        QVERIFY(QMetaObject::invokeMethod(&errors, "flushWarnings"));
        QCOMPARE(errors.count(), 0);
        msg_Info(m_intf, "Info is not a warning");
        msg_Err(m_intf, "A raw error is not a minor warning");
        QVERIFY(QMetaObject::invokeMethod(&errors, "flushWarnings"));
        QCOMPARE(errors.count(), 0);
        msg_Warn(m_intf, "New warning");
        QTRY_COMPARE(errors.count(), 1);
    }

    void testWarningObserverShutdownDuringLogging() {
        var_SetBool(m_intf, "qt-minor-warnings", true);
        auto errors = std::make_unique<DialogErrorModel>(m_intf);
        std::atomic<bool> running{true};
        auto producer = std::thread([&] {
            while (running.load()) msg_Warn(m_intf, "Shutdown warning");
        });
        QTest::qWait(10);
        errors.reset();
        running.store(false);
        producer.join();
    }

    void testErrorHistoryWithNotificationsOff() {
        DialogErrorModel errors(m_intf);
        QSignalSpy preferences(&errors, &DialogErrorModel::notificationsEnabledChanged);
        var_SetBool(m_intf, "qt-error-dialogs", false);
        errors.refreshNotificationPreference();
        QVERIFY(!errors.notificationsEnabled());
        vlc_dialog_display_error(m_intf, "Hidden error", "Error details");
        QTRY_COMPARE(errors.count(), 1);
        QCOMPARE(errors.data(errors.index(0), DialogErrorModel::DIALOG_TITLE).toString(), "Hidden error");
        QCOMPARE(errors.repeatedMessageCount(), 0);
        QVERIFY(errors.notificationText().isEmpty());
        var_SetBool(m_intf, "qt-error-dialogs", true);
        errors.refreshNotificationPreference();
        vlc_dialog_display_error(m_intf, "Visible error", "Error details");
        QTRY_COMPARE(errors.count(), 2);
        QCOMPARE(errors.repeatedMessageCount(), 1);
        QCOMPARE(errors.notificationText(), "Visible error");
        var_SetBool(m_intf, "qt-error-dialogs", false);
        errors.refreshNotificationPreference();
        QCOMPARE(errors.repeatedMessageCount(), 0);
        QCOMPARE(errors.count(), 2);
        QCOMPARE(preferences.count(), 3);
    }

    void testNotificationContextAndCopy() {
        const QString saved = QGuiApplication::clipboard()->text();
        const auto restore = qScopeGuard([saved] { QGuiApplication::clipboard()->setText(saved); });
        DialogErrorModel errors(m_intf);
        errors.pushError("Cannot open <fixture>", "First file & full details");
        QCOMPARE(errors.notificationTitle(), "Cannot open <fixture>");
        QCOMPARE(errors.notificationDetails(), "First file & full details");
        QVERIFY(errors.copyNotification());
        QCOMPARE(QGuiApplication::clipboard()->text(), "Cannot open <fixture>\nFirst file & full details");
        errors.pushError("Cannot open <fixture>", "First file & full details");
        QCOMPARE(errors.repeatedMessageCount(), 2);
        QVERIFY(errors.copyNotification());
        QCOMPARE(QGuiApplication::clipboard()->text(), "[2] Cannot open <fixture>\nFirst file & full details");
        errors.pushError("Cannot open <fixture>", "A different file");
        QCOMPARE(errors.repeatedMessageCount(), 1);
        QCOMPARE(errors.notificationDetails(), "A different file");
        QCOMPARE(errors.count(), 3);
        QCOMPARE(errors.data(errors.index(0), DialogErrorModel::DIALOG_TEXT).toString(), "First file & full details");
        errors.resetRepeatedMessageCount();
        QGuiApplication::clipboard()->setText("Keep clipboard");
        QVERIFY(!errors.copyNotification());
        QCOMPARE(QGuiApplication::clipboard()->text(), "Keep clipboard");
        var_SetBool(m_intf, "qt-error-dialogs", false);
        errors.refreshNotificationPreference();
        errors.pushError("Hidden notice", "History only");
        QVERIFY(!errors.copyNotification());
        QCOMPARE(QGuiApplication::clipboard()->text(), "Keep clipboard");
        QCOMPARE(errors.count(), 4);
    }

    void testWarningNotificationContextAndCopy() {
        const QString saved = QGuiApplication::clipboard()->text();
        const auto restore = qScopeGuard([saved] { QGuiApplication::clipboard()->setText(saved); });
        var_SetBool(m_intf, "qt-minor-warnings", true);
        DialogErrorModel errors(m_intf);
        msg_Warn(m_intf, "First <literal> warning");
        msg_Warn(m_intf, "Second warning");
        QVERIFY(QMetaObject::invokeMethod(&errors, "flushWarnings"));
        QVERIFY(errors.notificationIsWarning());
        QVERIFY(errors.notificationTitle().startsWith("Warning"));
        QVERIFY(errors.notificationDetails().contains("First <literal> warning"));
        QVERIFY(errors.notificationDetails().contains("2 warnings"));
        QCOMPARE(errors.repeatedMessageCount(), 2);
        QVERIFY(errors.copyNotification());
        QVERIFY(QGuiApplication::clipboard()->text().startsWith("[2] Warning"));
        QVERIFY(QGuiApplication::clipboard()->text().contains("First <literal> warning"));
        var_SetBool(m_intf, "qt-minor-warnings", false);
        errors.refreshNotificationPreference();
        QVERIFY(!errors.copyNotification());
    }

    void testQuestion_data() {
        QTest::addColumn<UserAnswer>("userReply");
        QTest::addColumn<int>("expectedReply");

        QTest::newRow("action1") << PostAction1 << 1;
        QTest::newRow("action2") << PostAction2 << 2;
        QTest::newRow("dismiss") << Dismiss << 0;
        QTest::newRow("wrongAnswer") << PostLogin << VLC_EGENERIC;
    }

    //failed state when no renderer manager is found
    void testQuestion() {
        QFETCH(UserAnswer, userReply);
        QFETCH(int, expectedReply);

        QObject::connect(m_dialog, &VLCDialog::question, this,
                         [&](DialogId dialogId, const QString& title,
                            const QString& text, VLCDialog::QuestionType type,
                            const QString& cancel, const QString& action1, const QString& action2){

            QCOMPARE(action1, "action1 txt");
            QCOMPARE(action2, "action2 txt");
            QCOMPARE(cancel, "cancel txt");
            QCOMPARE(title, "title");
            QCOMPARE(text, "message");
            QCOMPARE(type, VLCDialog::QuestionType::QUESTION_WARNING);

            switch(userReply)
            {
            case PostAction1:
                m_dialog->post_action1(dialogId);
                break;
            case PostAction2:
                m_dialog->post_action2(dialogId);
                break;
            case Dismiss:
                m_dialog->dismiss(dialogId);
                break;
            case PostLogin:
                m_dialog->post_login(dialogId, "not", "expected"); //user answer the type
                break;
            }
        });

        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);

        std::atomic<bool> replied = false;
        int reply = -1;
        QThreadPool::globalInstance()->start([&](){
            reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_WARNING,
                                             "cancel txt", "action1 txt",
                                             "action2 txt", "title", "message");
            replied = true;
        });
        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        if (expectedReply == VLC_EGENERIC)
            QCOMPARE_LT(reply, 0);
        else
            QCOMPARE(reply, expectedReply);

        QCOMPARE(m_loginSpy->count(), 0);
        QCOMPARE(m_progressSpy->count(), 0);
        QCOMPARE(m_progressUpdatedSpy->count(), 0);
        QCOMPARE(m_canceledSpy->count(), 0);
    }

    //failed state when no renderer manager is found
    void testProgress_data() {
        QTest::addColumn<bool>("notificationsEnabled");
        QTest::newRow("enabled") << true;
        QTest::newRow("disabled") << false;
    }

    void testProgress() {
        QFETCH(bool, notificationsEnabled);
        var_SetBool(m_intf, "qt-error-dialogs", notificationsEnabled);
        QObject::connect(m_dialog, &VLCDialog::progress, this,
                         [&](DialogId, const QString& title, const QString& text,
                                bool b_indeterminate, float f_position, const QString& cancel){

            //QCOMPARE performs is fuzzy for floats
            QCOMPARE(b_indeterminate, false);
            QCOMPARE(f_position, 0.f);
            QCOMPARE(cancel, "cancel txt");
            QCOMPARE(title, "title");
            QCOMPARE(text, "message");
        });

        QObject::connect(m_dialog, &VLCDialog::progressUpdated, this,
                         [&](DialogId, float f_position, const QString& text){

            //QCOMPARE performs is fuzzy for floats
            QCOMPARE(f_position, 0.3f);
            QCOMPARE(text, "updated text");
        });

        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);

        std::atomic<bool> replied = false;
        vlc_dialog_id* dialogId;
        QThreadPool::globalInstance()->start([&](){
            dialogId = vlc_dialog_display_progress(m_intf,
                                                   false, 0.f,
                                                   "cancel txt", "title", "message");
            replied = true;
        });
        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        QVERIFY(dialogId != nullptr);

        replied = false;
        int reply;
        QThreadPool::globalInstance()->start([&](){
            reply = vlc_dialog_update_progress_text(m_intf, dialogId, 0.3f, "updated text");
            replied = true;
        });
        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        QCOMPARE(reply, VLC_SUCCESS);
        QCOMPARE(m_canceledSpy->count(), 0);

        replied = false;
        QThreadPool::globalInstance()->start([&](){
            vlc_dialog_release(m_intf, dialogId);
            replied = true;
        });
        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        // The cancellation reaches the UI thread through a queued signal.
        QTRY_COMPARE(m_canceledSpy->count(), 1);

        QCOMPARE(m_loginSpy->count(), 0);
        QCOMPARE(m_questionSpy->count(), 0);
    }

    void testProviderChangeCancelsOwnedProgress() {
        auto *model = VLCDialogModel::getInstance<false>();
        QObject scope;
        DialogId pending;
        bool hasPending = false;
        connect(m_dialog, &VLCDialog::progress, &scope,
                [&](DialogId id, const QString &, const QString &, bool, float,
                    const QString &) {
            pending = id;
            hasPending = true;
        });
        connect(m_dialog, &VLCDialog::cancelAll, &scope, [&] {
            if (!hasPending) return;
            hasPending = false;
            m_dialog->dismiss(pending);
        });
        model->setProvider(m_dialog);
        auto *first = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                                  "Cancel", "First", "Work");
        QVERIFY(first);
        QVERIFY(hasPending);
        VLCDialog replacement;
        model->setProvider(&replacement);
        // The model must release the UI reference as the old screen disconnects.
        QVERIFY(!hasPending);
        QVERIFY(vlc_dialog_is_cancelled(m_intf, first));
        vlc_dialog_release(m_intf, first);

        QSignalSpy requests(&replacement, &VLCDialog::progress);
        auto *second = vlc_dialog_display_progress(m_intf, false, 0.5f,
                                                   "Cancel", "Second", "Work");
        QVERIFY(second);
        QCOMPARE(requests.count(), 1);
        replacement.dismiss(requests.first().first().value<DialogId>());
        QVERIFY(vlc_dialog_is_cancelled(m_intf, second));
        vlc_dialog_release(m_intf, second);
        model->setProvider(m_dialog);
    }

    void testProviderChangeBeforeQueuedDelivery() {
        auto *model = VLCDialogModel::getInstance<false>();
        QObject screen;
        DialogId knownId;
        bool delivered = false;
        connect(m_dialog, &VLCDialog::progress, &screen,
                [&](DialogId id, const QString &, const QString &, bool, float,
                    const QString &) {
            knownId = id;
            delivered = true;
        }, Qt::QueuedConnection);
        connect(m_dialog, &VLCDialog::cancelAll, &screen, [&] {
            if (delivered) m_dialog->dismiss(knownId);
        });
        model->setProvider(m_dialog);
        vlc_dialog_id *id = nullptr;
        std::thread producer([&] {
            id = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                             "Cancel", "Queued", "Work");
        });
        producer.join();
        QVERIFY(id);
        QVERIFY(!delivered);
        VLCDialog replacement;
        model->setProvider(&replacement);
        const bool cancelled = vlc_dialog_is_cancelled(m_intf, id);
        // Bound a failing regression without stealing a reference from the model.
        if (!cancelled) VLCDialogModel::killInstance();
        vlc_dialog_release(m_intf, id);
        if (cancelled) model->setProvider(m_dialog);
        QVERIFY2(cancelled, "Provider change must cancel a request that the old UI has not received yet");
    }

    void testProviderDestroyedBeforeDelivery() {
        auto *model = VLCDialogModel::getInstance<false>();
        auto screen = std::make_unique<VLCDialog>();
        model->setProvider(screen.get());
        vlc_dialog_id *id = nullptr;
        std::thread producer([&] {
            id = vlc_dialog_display_progress(m_intf, false, 0.5f,
                                             "Cancel", "Destroyed screen", "Work");
        });
        producer.join();
        QVERIFY(id);
        screen.reset();
        const bool cancelled = vlc_dialog_is_cancelled(m_intf, id);
        const bool providerCleared = model->getProvider() == nullptr;
        if (!cancelled) VLCDialogModel::killInstance();
        vlc_dialog_release(m_intf, id);
        if (cancelled) model->setProvider(m_dialog);
        QVERIFY(providerCleared);
        QCoreApplication::processEvents();
        QVERIFY(cancelled);
        QCOMPARE(m_progressSpy->count(), 0);
    }

    void testModelDestroyedBeforeDelivery() {
        auto *model = VLCDialogModel::getInstance<false>();
        model->setProvider(m_dialog);
        vlc_dialog_id *id = nullptr;
        std::thread producer([&] {
            id = vlc_dialog_display_progress(m_intf, false, 0.5f,
                                             "Cancel", "Destroyed model", "Work");
        });
        producer.join();
        QVERIFY(id);
        VLCDialogModel::killInstance();
        const bool cancelled = vlc_dialog_is_cancelled(m_intf, id);
        if (!cancelled) vlc_dialog_id_dismiss(id);
        vlc_dialog_release(m_intf, id);
        QCoreApplication::processEvents();
        QVERIFY(cancelled);
        QCOMPARE(m_progressSpy->count(), 0);
    }

    void testCancellationBeforeDelivery() {
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);
        QObject screen;
        connect(m_dialog, &VLCDialog::cancelled, &screen,
                [&](DialogId id) { m_dialog->dismiss(id); });
        std::thread producer([&] {
            auto *id = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                                  "Cancel", "Quick work", "Starting");
            if (!id) return;
            vlc_dialog_update_progress_text(m_intf, id, 0.75f, "Already done");
            vlc_dialog_release(m_intf, id);
        });
        producer.join();
        QTRY_COMPARE(m_canceledSpy->count(), 1);
        QCOMPARE(m_progressSpy->count(), 0);
        QCOMPARE(m_progressUpdatedSpy->count(), 0);
    }

    void testWorkerDeliveryOwnsTextAndRunsOnGuiThread() {
        auto *model = VLCDialogModel::getInstance<false>();
        model->setProvider(m_dialog);
        QObject screen;
        bool onGuiThread = false;
        connect(m_dialog, &VLCDialog::progress, &screen,
                [&](DialogId, const QString &, const QString &, bool, float,
                    const QString &) {
            onGuiThread = QThread::currentThread() == model->thread();
        }, Qt::DirectConnection);
        vlc_dialog_id *id = nullptr;
        std::thread producer([&] {
            QByteArray title("Temporary <title>"), text("Temporary message & path");
            id = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                             "Cancel", title.constData(), "%s",
                                             text.constData());
            // Native callback storage may change as soon as the call returns.
            title.fill('x');
            text.fill('y');
        });
        producer.join();
        QVERIFY(id);
        QCOMPARE(m_progressSpy->count(), 0);
        QTRY_COMPARE(m_progressSpy->count(), 1);
        const auto args = m_progressSpy->first();
        QCOMPARE(args[1].toString(), QStringLiteral("Temporary <title>"));
        QCOMPARE(args[2].toString(), QStringLiteral("Temporary message & path"));
        QVERIFY(onGuiThread);
        m_dialog->dismiss(args[0].value<DialogId>());
        QVERIFY(vlc_dialog_is_cancelled(m_intf, id));
        vlc_dialog_release(m_intf, id);
    }

    void testStaleHandleCannotAnswerReusedAddress() {
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);
        auto *first = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                                  "Cancel", "First", "Work");
        QVERIFY(first);
        const auto oldHandle = m_progressSpy->last()[0].value<DialogId>();
        m_dialog->dismiss(oldHandle);
        vlc_dialog_release(m_intf, first);
        auto *second = vlc_dialog_display_progress(m_intf, false, 0.5f,
                                                   "Cancel", "Second", "Work");
        QVERIFY(second);
        const auto newHandle = m_progressSpy->last()[0].value<DialogId>();
        QVERIFY(newHandle.m_serial != oldHandle.m_serial);
        // Model a native allocator reusing a prior address deterministically.
        m_dialog->dismiss(DialogId(second, oldHandle.m_serial));
        QVERIFY(!vlc_dialog_is_cancelled(m_intf, second));
        m_dialog->dismiss(newHandle);
        QVERIFY(vlc_dialog_is_cancelled(m_intf, second));
        // A second reply after release must be ignored without dereferencing it.
        vlc_dialog_release(m_intf, second);
        m_dialog->dismiss(newHandle);
    }

    void testStaleHandleCannotCrossModelRecreation() {
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);
        auto *first = vlc_dialog_display_progress(m_intf, false, 0.25f,
                                                  "Cancel", "Before restart", "Work");
        QVERIFY(first);
        const auto oldHandle = m_progressSpy->last()[0].value<DialogId>();
        m_dialog->dismiss(oldHandle);
        vlc_dialog_release(m_intf, first);
        VLCDialogModel::killInstance();
        VLCDialogModel::getInstance(m_intf)->setProvider(m_dialog);
        auto *second = vlc_dialog_display_progress(m_intf, false, 0.5f,
                                                   "Cancel", "After restart", "Work");
        QVERIFY(second);
        const auto newHandle = m_progressSpy->last()[0].value<DialogId>();
        const bool unique = newHandle.m_serial != oldHandle.m_serial;
        m_dialog->dismiss(DialogId(second, oldHandle.m_serial));
        const bool staleRejected = !vlc_dialog_is_cancelled(m_intf, second);
        m_dialog->dismiss(newHandle);
        vlc_dialog_release(m_intf, second);
        QVERIFY2(unique, "Handles must stay distinct when the model is recreated");
        QVERIFY(staleRejected);
    }

    void testLateBinding() {
        QObject::connect(m_dialog, &VLCDialog::question, this,
                         [&](DialogId dialogId, const QString& ,
                             const QString& , VLCDialog::QuestionType ,
                             const QString& , const QString& , const QString& ){
            m_dialog->post_action1(dialogId);
        });

        std::atomic<bool> replied = false;
        int reply = -1;
        QThreadPool::globalInstance()->start([&](){
            reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_WARNING,
                                             "cancel txt", "action1 txt",
                                             "action2 txt", "title", "message");
            replied = true;
        });
        //wait for the question to be asked
        //the dialog should block until VLCDialog is provided
        QTest::qWait(20);

        //set the dialog afterwards
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);

        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));
        //user replied with Action1
        QCOMPARE(reply, 1);

        QCOMPARE(m_loginSpy->count(), 0);
        QCOMPARE(m_progressSpy->count(), 0);
        QCOMPARE(m_progressUpdatedSpy->count(), 0);
        QCOMPARE(m_canceledSpy->count(), 0);
    }

    void testModelDestruction() {
        std::atomic<bool> replied = false;
        int reply = -1;
        QThreadPool::globalInstance()->start([&](){
            reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_WARNING,
                                             "cancel txt", "action1 txt",
                                             "action2 txt", "title", "message");
            replied = true;
        });
        //wait for the question to be asked
        //the dialog should block until VLCDialog is provided
        QTest::qWait(20);

        VLCDialogModel::killInstance();

        QVERIFY(QTest::qWaitFor([&replied](){ return replied == true; }));

        QCOMPARE(reply, 0);
    }

    void testSameThreadWithoutDialog()
    {
        int reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_WARNING,
                                             "cancel txt", "action1 txt",
                                             "action2 txt", "title", "message");
        QCOMPARE(reply, 0);
    }

    void testSameThreadWithDialog()
    {
        QObject::connect(m_dialog, &VLCDialog::question, this,
                         [&](DialogId dialogId, const QString& ,
                             const QString& , VLCDialog::QuestionType ,
                             const QString& , const QString& , const QString& ){
            m_dialog->post_action1(dialogId);
        });
        VLCDialogModel::getInstance<false>()->setProvider(m_dialog);

        int reply = vlc_dialog_wait_question(m_intf, VLC_DIALOG_QUESTION_WARNING,
                                             "cancel txt", "action1 txt",
                                             "action2 txt", "title", "message");
        QCOMPARE(reply, 1);
    }

private:
    std::unique_ptr<VLCTestingEnv> m_env;
    qt_intf_t* m_intf = nullptr;

    VLCDialog* m_dialog = nullptr;

    std::unique_ptr<QSignalSpy> m_loginSpy;
    std::unique_ptr<QSignalSpy> m_questionSpy;
    std::unique_ptr<QSignalSpy> m_progressSpy;
    std::unique_ptr<QSignalSpy> m_progressUpdatedSpy;
    std::unique_ptr<QSignalSpy> m_canceledSpy;
};

int main(int argc, char **argv)
{
#if !defined(Q_OS_WIN) && !defined(Q_OS_MACOS)
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
#endif
    QGuiApplication app(argc, argv);
    TestVLCDialogModel test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_vlc_dialog_model.moc"
