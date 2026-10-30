// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef VLC_QT_HANG_REPORTER_HPP
#define VLC_QT_HANG_REPORTER_HPP

#include <QString>
#include <memory>

struct vlc_object_t;

// A diagnostic for Windows, off by default (--qt-hang-report). While it runs,
// a GUI-thread timer marks the thread alive every 100 ms. When the mark is
// older than 500 ms, a watchdog thread takes up to 40 samples of the GUI
// thread's call stack, 250 ms apart, and writes them as module+offset frames
// to vlc-hang-report.txt in the temporary folder. The GUI thread is only
// suspended long enough to copy its registers and stack; no lock is taken
// while it is suspended. On other systems start() returns null.
class HangReporter
{
public:
    static std::unique_ptr<HangReporter> start(vlc_object_t* log);
    ~HangReporter();
    QString path() const;

    struct Native;
private:
    explicit HangReporter(std::unique_ptr<Native>);
    std::unique_ptr<Native> m_native;
};

#endif
