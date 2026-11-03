// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include "hang_reporter.hpp"
#include "qt.hpp"
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

#ifdef _WIN32
# include <windows.h>
# include <algorithm>
# include <atomic>
# include <cstring>
# include <cstdio>
# include <thread>
# include <vector>

struct HangReporter::Native
{
    vlc_object_t* log = nullptr;
    HANDLE gui = nullptr;
    DWORD64 stackBase = 0;
    QString path;
    FILE* file = nullptr;
    QTimer heartbeat;
    std::atomic<ULONGLONG> beat{0};
    std::atomic_bool stop{false};
    std::thread watchdog;
    std::vector<unsigned char> copy = std::vector<unsigned char>(512 * 1024);

    ~Native()
    {
        stop = true;
        if (watchdog.joinable()) watchdog.join();
        if (file) fclose(file);
        if (gui) CloseHandle(gui);
    }

    std::string frameName(DWORD64 address) const
    {
        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(address), &module) || !module)
            return QStringLiteral("0x%1").arg(address, 0, 16).toStdString();
        wchar_t name[MAX_PATH] = {};
        GetModuleFileNameW(module, name, MAX_PATH);
        const auto base = QFileInfo(QString::fromWCharArray(name)).fileName();
        return QStringLiteral("%1+0x%2").arg(base).arg(address - reinterpret_cast<DWORD64>(module), 0, 16).toStdString();
    }

    // Unwind a copy of the GUI thread's stack. Registers that point into the
    // original stack are moved into the copy for each step, so the walk never
    // reads the live stack, and it stops at anything it cannot follow.
    std::vector<DWORD64> unwind(CONTEXT context, DWORD64 low, size_t size)
    {
        std::vector<DWORD64> frames{context.Rip};
        const auto high = low + size;
        const auto delta = reinterpret_cast<DWORD64>(copy.data()) - low;
        const auto inStack = [low, high](DWORD64 value) { return value >= low && value + 8 <= high; };
        for (int depth = 0; depth < 64; ++depth) {
            if (!inStack(context.Rsp)) break;
            DWORD64 image = 0;
            auto* function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (!function) {
                // A leaf function: the return address is at the stack pointer.
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp + delta);
                context.Rsp += 8;
            } else {
                const auto* info = reinterpret_cast<const unsigned char*>(image + function->UnwindData);
                const int frameRegister = info[3] & 0x0f;
                // Only a frame pointer in RBP that points into the stack can be followed.
                if (frameRegister != 0 && (frameRegister != 5 || !inStack(context.Rbp))) break;
                CONTEXT moved = context;
                moved.Rsp += delta;
                if (inStack(moved.Rbp)) moved.Rbp += delta;
                PVOID handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &moved, &handler, &establisher, nullptr);
                if (moved.Rsp >= delta + low) moved.Rsp -= delta;
                if (moved.Rbp >= delta + low && moved.Rbp < delta + high) moved.Rbp -= delta;
                context = moved;
            }
            if (!context.Rip) break;
            frames.push_back(context.Rip);
        }
        return frames;
    }

    void sample(unsigned stall, unsigned index, ULONGLONG stalled)
    {
        if (SuspendThread(gui) == DWORD(-1)) return;
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_FULL;
        size_t size = 0;
        const bool captured = GetThreadContext(gui, &context);
        if (captured && context.Rsp < stackBase) {
            size = size_t(std::min<DWORD64>(stackBase - context.Rsp, copy.size()));
            memcpy(copy.data(), reinterpret_cast<const void*>(context.Rsp), size);
        }
        ResumeThread(gui);
        if (!captured || !size) return;
        const auto frames = unwind(context, context.Rsp, size);
        fprintf(file, "stall %u sample %u at %llu ms:\n", stall, index, static_cast<unsigned long long>(stalled));
        for (const auto frame : frames) fprintf(file, "  %s\n", frameName(frame).c_str());
        fflush(file);
    }

    void run()
    {
        unsigned stall = 0, samples = 0;
        ULONGLONG stalledSince = 0, nextSample = 0, longest = 0;
        while (!stop) {
            Sleep(50);
            const auto now = GetTickCount64(), last = beat.load();
            const auto stalled = now > last ? now - last : 0;
            if (stalled >= 500) {
                if (last != stalledSince) {
                    stalledSince = last; samples = 0; nextSample = now; longest = 0; ++stall;
                    fprintf(file, "stall %u started\n", stall);
                }
                longest = stalled;
                if (samples < 40 && now >= nextSample) {
                    sample(stall, ++samples, stalled);
                    nextSample = now + 250;
                }
            } else if (stalledSince) {
                fprintf(file, "stall %u ended after %llu ms\n\n", stall, static_cast<unsigned long long>(longest + 100));
                fflush(file);
                msg_Warn(log, "The interface was not responding for %llu ms; see %s",
                         static_cast<unsigned long long>(longest + 100), qtu(path));
                stalledSince = 0;
            }
        }
    }
};

std::unique_ptr<HangReporter> HangReporter::start(vlc_object_t* log)
{
    auto native = std::make_unique<Native>();
    native->log = log;
    if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &native->gui,
            THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, 0))
        return {};
    native->stackBase = reinterpret_cast<DWORD64>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    native->path = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
        .filePath(QStringLiteral("vlc-hang-report.txt"));
    native->file = _wfopen(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(native->path).utf16()), L"w");
    if (!native->file) return {};
    fprintf(native->file, "VLC interface stall report. Frames are module+offset.\n\n");
    fflush(native->file);
    native->beat = GetTickCount64();
    auto* raw = native.get();
    native->heartbeat.setInterval(100);
    QObject::connect(&native->heartbeat, &QTimer::timeout, [raw] { raw->beat = GetTickCount64(); });
    native->heartbeat.start();
    native->watchdog = std::thread([raw] { raw->run(); });
    msg_Info(log, "Interface stall reports are written to %s", qtu(native->path));
    return std::unique_ptr<HangReporter>(new HangReporter(std::move(native)));
}
QString HangReporter::path() const { return m_native->path; }
#else
struct HangReporter::Native {};
std::unique_ptr<HangReporter> HangReporter::start(vlc_object_t*) { return {}; }
QString HangReporter::path() const { return {}; }
#endif

HangReporter::HangReporter(std::unique_ptr<Native> native) : m_native(std::move(native)) {}
HangReporter::~HangReporter() = default;
