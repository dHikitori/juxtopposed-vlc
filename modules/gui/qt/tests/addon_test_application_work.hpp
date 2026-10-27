// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "addons/addon_stream_lifetime.hpp"
#include <QCoreApplication>
#include <QtGlobal>
#include <chrono>

class AddonTestApplicationWork final
{
public:
    void start()
    {
        if (m_registered) return;
        if (!vlc::addons::AddonStreamLifetime::registerApplication(m_lifetime.handle()))
            qFatal("The test application cannot register its add-on work owner.");
        m_registered = true;
    }
    void finish()
    {
        if (!m_registered) return;
        m_lifetime.close();
        if (!m_lifetime.handle().waitQt(std::chrono::seconds(30)))
            qFatal("Add-on work did not join before test application cleanup.");
        if (!vlc::addons::AddonStreamLifetime::unregisterApplication(m_lifetime.handle()))
            qFatal("The test application cannot release its add-on work owner.");
        m_registered = false;
    }
    ~AddonTestApplicationWork() { finish(); }
private:
    vlc::addons::AddonStreamLifetime m_lifetime;
    bool m_registered = false;
};

#define HAKONE_QTEST_MAIN(Test, Application) \
    int main(int argc, char** argv) \
    { \
        QTEST_SET_MAIN_SOURCE_PATH \
        Application app(argc, argv); \
        AddonTestApplicationWork work; work.start(); \
        Test test; return QTest::qExec(&test, argc, argv); \
    }
