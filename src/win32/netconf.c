/*****************************************************************************
 * netconf.c : Network configuration
 *****************************************************************************
 * Copyright (C) 2001-2008 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <string.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_network.h>
#include <vlc_url.h>
#include <vlc_charset.h>
#include <vlc_interrupt.h>

#include "proxy.h"
#include "../network/proxy.h"

#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
#include <winhttp.h>
#include <stdatomic.h>

struct proxy_lookup
{
    HANDLE completed;
    HANDLE closed;
    atomic_bool ready;
    DWORD error;
};

static void CALLBACK proxy_status(HINTERNET handle, DWORD_PTR context,
                                  DWORD status, void *data, DWORD length)
{
    VLC_UNUSED(handle);
    struct proxy_lookup *lookup = (void *)context;
    if (lookup == NULL)
        return;

    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING)
    {
        SetEvent(lookup->closed);
        return;
    }
    if (status == WINHTTP_CALLBACK_STATUS_GETPROXYFORURL_COMPLETE)
        lookup->error = ERROR_SUCCESS;
    else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR &&
             length == sizeof(WINHTTP_ASYNC_RESULT))
        lookup->error = ((WINHTTP_ASYNC_RESULT *)data)->dwError;
    else
        return;

    atomic_store_explicit(&lookup->ready, true, memory_order_release);
    SetEvent(lookup->completed);
}

static void proxy_interrupt(void *opaque)
{
    struct proxy_lookup *lookup = opaque;
    SetEvent(lookup->completed);
}

/* The asynchronous resolver is optional so that explicit/manual proxy support
 * still works on Windows 7. Never use the non-cancellable synchronous PAC API. */
static bool proxy_auto(const wchar_t *url,
                       const WINHTTP_CURRENT_USER_IE_PROXY_CONFIG *config,
                       char **proxy)
{
    HMODULE module = GetModuleHandleW(L"winhttp.dll");
    DWORD (WINAPI *create_resolver)(HINTERNET, HINTERNET *) =
        (void *)GetProcAddress(module, "WinHttpCreateProxyResolver");
    DWORD (WINAPI *get_proxy)(HINTERNET, const wchar_t *,
                              WINHTTP_AUTOPROXY_OPTIONS *, DWORD_PTR) =
        (void *)GetProcAddress(module, "WinHttpGetProxyForUrlEx");
    DWORD (WINAPI *get_result)(HINTERNET, WINHTTP_PROXY_RESULT *) =
        (void *)GetProcAddress(module, "WinHttpGetProxyResult");
    void (WINAPI *free_result)(WINHTTP_PROXY_RESULT *) =
        (void *)GetProcAddress(module, "WinHttpFreeProxyResult");
    if (!create_resolver || !get_proxy || !get_result || !free_result)
        return false;

    WINHTTP_AUTOPROXY_OPTIONS options = {0};
    if (config->lpszAutoConfigUrl && *config->lpszAutoConfigUrl)
    {
        options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
        options.lpszAutoConfigUrl = config->lpszAutoConfigUrl;
    }
    if (config->fAutoDetect)
    {
        options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
        options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP |
                                    WINHTTP_AUTO_DETECT_TYPE_DNS_A;
    }
    if (!options.dwFlags)
        return false;

    bool resolved = false;
    HINTERNET session = WinHttpOpen(L"Hakone", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC);
    if (!session)
        return false;

    struct proxy_lookup lookup = {
        .completed = CreateEventW(NULL, TRUE, FALSE, NULL),
        .closed = CreateEventW(NULL, TRUE, FALSE, NULL),
        .ready = ATOMIC_VAR_INIT(false),
    };
    if (!lookup.completed || !lookup.closed)
        goto out;

    if (WinHttpSetStatusCallback(session, proxy_status,
            WINHTTP_CALLBACK_FLAG_REQUEST_ERROR |
            WINHTTP_CALLBACK_FLAG_GETPROXYFORURL_COMPLETE |
            WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK)
        goto out;

    HINTERNET resolver;
    if (create_resolver(session, &resolver) != ERROR_SUCCESS)
        goto out;

    DWORD_PTR context = (DWORD_PTR)&lookup;
    if (!WinHttpSetOption(resolver, WINHTTP_OPTION_CONTEXT_VALUE, &context,
                          sizeof(context)))
    {
        WinHttpCloseHandle(resolver);
        goto out;
    }

    vlc_interrupt_register(proxy_interrupt, &lookup);
    DWORD error = get_proxy(resolver, url, &options, context);
    DWORD wait = error == ERROR_IO_PENDING
               ? WaitForSingleObject(lookup.completed, 5000) : WAIT_FAILED;
    bool interrupted = vlc_interrupt_unregister() != 0;
    /* Do not select a fallback proxy after the input has been stopped. */
    resolved = interrupted;
    if (!interrupted && wait == WAIT_OBJECT_0 &&
        atomic_load_explicit(&lookup.ready, memory_order_acquire) &&
        lookup.error == ERROR_SUCCESS)
    {
        WINHTTP_PROXY_RESULT result;
        if (get_result(resolver, &result) == ERROR_SUCCESS)
        {
            /* The public VLC interface returns one proxy. Preserve PAC order,
             * including a DIRECT entry before a proxy, rather than skipping it. */
            if (result.cEntries)
            {
                const WINHTTP_PROXY_RESULT_ENTRY *entry = result.pEntries;
                if (!entry->fProxy)
                    resolved = true;
                else if ((entry->ProxyScheme == INTERNET_SCHEME_HTTP ||
                          entry->ProxyScheme == INTERNET_SCHEME_HTTPS) &&
                         entry->pwszProxy)
                {
                    char *host = FromWide(entry->pwszProxy);
                    if (host)
                    {
                        const char *scheme = entry->ProxyScheme == INTERNET_SCHEME_HTTPS
                                           ? "https" : "http";
                        if (asprintf(proxy, strchr(host, ':') && host[0] != '['
                                     ? "%s://[%s]:%u" : "%s://%s:%u",
                                     scheme, host, entry->ProxyPort) >= 0)
                            resolved = true;
                        free(host);
                    }
                }
            }
            free_result(&result);
        }
    }

    WinHttpCloseHandle(resolver);
    /* HANDLE_CLOSING is the final callback. Keep the context and PAC strings
     * alive until cancellation has finished, including on timeout/interruption. */
    WaitForSingleObject(lookup.closed, INFINITE);
out:
    WinHttpCloseHandle(session);
    if (lookup.completed)
        CloseHandle(lookup.completed);
    if (lookup.closed)
        CloseHandle(lookup.closed);
    return resolved;
}

static char *proxy_system(const char *target)
{
    if (vlc_killed())
        return NULL;
    vlc_url_t url;
    if (vlc_UrlParse(&url, target) < 0)
    {
        vlc_UrlClean(&url);
        return NULL;
    }
    char *proxy = NULL;
    if (!url.psz_protocol || !url.psz_host ||
        (strcasecmp(url.psz_protocol, "http") &&
         strcasecmp(url.psz_protocol, "https")))
        goto clean_url;

    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG config = {0};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&config))
        goto clean_config;

    wchar_t *wide_url = ToWide(target);
    bool resolved = wide_url && proxy_auto(wide_url, &config, &proxy);
    free(wide_url);
    if (!resolved)
    {
        char *bypass = config.lpszProxyBypass ? FromWide(config.lpszProxyBypass)
                                             : NULL;
        unsigned port = url.i_port ? url.i_port
                       : !strcasecmp(url.psz_protocol, "https") ? 443 : 80;
        if (!proxy_bypasses(bypass, url.psz_host, port))
        {
            char *list = config.lpszProxy ? FromWide(config.lpszProxy) : NULL;
            char *address = proxy_for_scheme(list, url.psz_protocol);
            if (address)
            {
                if (strstr(address, "://"))
                    proxy = strdup(address);
                else if (asprintf(&proxy, "http://%s", address) < 0)
                    proxy = NULL;
            }
            free(address);
            free(list);
        }
        free(bypass);
    }
clean_config:
    GlobalFree(config.lpszProxy);
    GlobalFree(config.lpszProxyBypass);
    GlobalFree(config.lpszAutoConfigUrl);
clean_url:
    vlc_UrlClean(&url);
    return proxy;
}
#endif

static char *proxy_find(vlc_object_t *obj, const char *psz_url)
{
    bool configured;
    char *proxy = vlc_proxy_GetConfigured(obj, &configured);
    if (!configured)
    {
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP)
        return psz_url ? proxy_system(psz_url) : NULL;
#else
        VLC_UNUSED(psz_url);
        return NULL;
#endif
    }

    return proxy;
}

char *vlc_getProxyUrl(const char *url)
{
    return proxy_find(NULL, url);
}

char *vlc_getProxyUrlForObject(vlc_object_t *obj, const char *url)
{
    assert(obj != NULL);
    return proxy_find(obj, url);
}
