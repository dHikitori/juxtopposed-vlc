// SPDX-License-Identifier: GPL-2.0-or-later
#include "addon_stream_dns.hpp"
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif
#include <algorithm>
#include <cstring>
#include <memory>

namespace vlc::addons {
std::vector<AddonStreamDns::Address> AddonStreamDns::resolve(const std::string& hostname)
{
    if(hostname.empty() || hostname.size()>253) return {};
    for(const unsigned char value:hostname)
        if(!((value>='a' && value<='z') || (value>='A' && value<='Z') || (value>='0' && value<='9') || value=='-' || value=='.')) return {};
#ifdef _WIN32
    struct Winsock {
        bool ready=false;
        Winsock() { WSADATA data{}; ready=WSAStartup(MAKEWORD(2,2),&data)==0; }
        ~Winsock() { if(ready) WSACleanup(); }
    } winsock;
    if(!winsock.ready) return {};
#endif
    addrinfo hints{}; hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM; hints.ai_protocol=IPPROTO_TCP;
    addrinfo* raw=nullptr;
    if(getaddrinfo(hostname.c_str(),nullptr,&hints,&raw)!=0) return {};
    const auto release=[](addrinfo* value) { if(value) freeaddrinfo(value); };
    const std::unique_ptr<addrinfo,decltype(release)> owned(raw,release);
    std::vector<Address> result;
    for(auto* entry=raw;entry;entry=entry->ai_next) {
        if(!entry->ai_addr) return {};
        Address value;
        if(entry->ai_family==AF_INET && entry->ai_addrlen>=sizeof(sockaddr_in)) {
            value.family=4; std::memcpy(value.bytes.data(),&reinterpret_cast<sockaddr_in*>(entry->ai_addr)->sin_addr,4);
        } else if(entry->ai_family==AF_INET6 && entry->ai_addrlen>=sizeof(sockaddr_in6)) {
            value.family=6; const auto* address=reinterpret_cast<sockaddr_in6*>(entry->ai_addr);
            std::memcpy(value.bytes.data(),&address->sin6_addr,16); value.scope=address->sin6_scope_id;
        } else return {};
        if(std::find(result.begin(),result.end(),value)==result.end()) result.push_back(value);
        if(result.size()>32) return {};
    }
    return result;
}
}
