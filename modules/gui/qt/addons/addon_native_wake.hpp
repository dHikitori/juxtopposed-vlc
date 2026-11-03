// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <functional>
#include <memory>
namespace vlc::addons {
// Qt owner wraps a native-only wake. Its callback can outlive this QObject;
// the OS handle stays retained until the last native callback has returned.
class AddonNativeWake final : public QObject {
public:
    explicit AddonNativeWake(std::function<void()>,QObject* parent=nullptr);
    ~AddonNativeWake() override;
    bool valid() const noexcept;
    std::function<void()> nativeCallback() const;
    void close();
private:
    struct Native;
    void activated();
    std::shared_ptr<Native> m_native;
    QObject* m_notifier=nullptr;
    std::function<void()> m_deliver;
    bool m_closed=false;
};
}
