// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace vlc::addons {
// Native DNS work has no Qt networking object or application reference.
class AddonStreamDns final
{
public:
    struct Address {
        unsigned family=0;
        std::array<unsigned char,16> bytes{};
        std::uint32_t scope=0;
        bool operator==(const Address& other) const noexcept {
            return family==other.family && bytes==other.bytes && scope==other.scope;
        }
    };
    // Use an ASCII host name. The network owner must validate every result.
    // The OS call can block. Run it through a retained plain-work permit.
    static std::vector<Address> resolve(const std::string&);
};
}
