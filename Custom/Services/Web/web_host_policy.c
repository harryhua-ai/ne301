#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "web_host_policy.h"

static char g_admin_hosts[WEB_HOST_POLICY_MAX_HOSTS][WEB_HOST_POLICY_HOST_LEN];
static uint32_t g_admin_host_count;
static char g_app_site_host[WEB_HOST_POLICY_HOST_LEN];
static aicam_bool_t g_app_site_explicit;

typedef struct {
    char host[WEB_HOST_POLICY_HOST_LEN];
    uint32_t port;
    aicam_bool_t valid;
} host_tuple_t;

static aicam_bool_t valid_port_tail(const char *s)
{
    uint32_t port = 0;
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return AICAM_FALSE;
    }
    for (i = 0; s[i] != '\0'; i++) {
        if (!isdigit((unsigned char)s[i]) || i >= 5u) {
            return AICAM_FALSE;
        }
        port = port * 10u + (uint32_t)(s[i] - '0');
    }
    return port >= 1u && port <= 65535u;
}

static void normalize_host(const char *host_header, char *out, size_t cap)
{
    size_t i = 0;
    size_t n = 0;
    size_t len;

    if (out != NULL && cap > 0u) {
        out[0] = '\0';
    }
    if (host_header == NULL || out == NULL || cap == 0u) {
        return;
    }
    len = strlen(host_header);
    if (len > 2u && host_header[0] == '[') {
        const char *end = strchr(host_header, ']');
        if (end != NULL) {
            const char *tail = end + 1;
            size_t body = (size_t)(end - host_header) - 1u;
            if (*tail != '\0') {
                if (*tail != ':' || valid_port_tail(tail + 1) != AICAM_TRUE) {
                    return;
                }
            }
            if (body >= cap || body == 0u) {
                return;
            }
            for (i = 0; i < body; i++) {
                out[i] = (char)tolower((unsigned char)host_header[i + 1u]);
            }
            out[body] = '\0';
            len = strlen(out);
            while (len > 0u && out[len - 1u] == '.') {
                out[--len] = '\0';
            }
            return;
        }
    }
    while (i < len) {
        char ch = host_header[i];
        if (ch == ':') {
            if (valid_port_tail(host_header + i + 1) != AICAM_TRUE) {
                out[0] = '\0';
                return;
            }
            break;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            out[0] = '\0';
            return;
        }
        if (n + 1u >= cap) {
            out[0] = '\0';
            return;
        }
        out[n++] = (char)tolower((unsigned char)ch);
        i++;
    }
    out[n] = '\0';
    len = strlen(out);
    while (len > 0u && out[len - 1u] == '.') {
        out[--len] = '\0';
    }
    if (n == 0u) {
        out[0] = '\0';
    }
}

static aicam_bool_t parse_authority(const char *s, host_tuple_t *out, uint32_t default_port)
{
    size_t i = 0;
    size_t host_start;
    size_t host_end;
    size_t len;
    uint32_t port = default_port;
    aicam_bool_t bracketed = AICAM_FALSE;

    if (s == NULL || out == NULL) {
        return AICAM_FALSE;
    }
    len = strlen(s);
    if (len == 0u || len >= 256u) {
        return AICAM_FALSE;
    }
    if (s[0] == '[') {
        const char *close = strchr(s, ']');
        if (close == NULL || close == s + 1) {
            return AICAM_FALSE;
        }
        for (i = 1; s[i] != ']'; i++) {
            if (s[i] == '@' || s[i] == '%' || s[i] == '/' || s[i] == '?' ||
                s[i] == '#' || s[i] == ' ') {
                return AICAM_FALSE;
            }
        }
        bracketed = AICAM_TRUE;
        host_start = 1;
        host_end = (size_t)(close - s);
        i = host_end + 1;
    } else {
        host_start = 0;
        for (i = 0; i < len; i++) {
            char ch = s[i];
            if (ch == ':') {
                break;
            }
            if (ch == '@' || ch == '%' || ch == '/' || ch == '?' || ch == '#' ||
                ch == '[' || ch == ']' || ch == ' ') {
                return AICAM_FALSE;
            }
        }
        host_end = i;
        if (host_end == 0u) {
            return AICAM_FALSE;
        }
    }
    if (i < len) {
        size_t port_start;
        if (s[i] != ':') {
            return AICAM_FALSE;
        }
        port_start = i + 1;
        if (port_start >= len) {
            return AICAM_FALSE;
        }
        port = 0;
        for (i = port_start; i < len; i++) {
            if (!isdigit((unsigned char)s[i])) {
                return AICAM_FALSE;
            }
            port = port * 10u + (uint32_t)(s[i] - '0');
            if (port > 65535u) {
                return AICAM_FALSE;
            }
        }
        if (port == 0u) {
            return AICAM_FALSE;
        }
    }
    if (!bracketed) {
        size_t h;
        if (host_end - host_start >= WEB_HOST_POLICY_HOST_LEN) {
            return AICAM_FALSE;
        }
        for (h = host_start; h < host_end; h++) {
            out->host[h - host_start] = (char)tolower((unsigned char)s[h]);
        }
        out->host[host_end - host_start] = '\0';
    } else {
        size_t h;
        if (host_end - host_start >= WEB_HOST_POLICY_HOST_LEN) {
            return AICAM_FALSE;
        }
        for (h = host_start; h < host_end; h++) {
            out->host[h - host_start] = (char)tolower((unsigned char)s[h]);
        }
        out->host[host_end - host_start] = '\0';
    }
    len = strlen(out->host);
    while (len > 0u && out->host[len - 1u] == '.') {
        out->host[--len] = '\0';
    }
    if (out->host[0] == '\0') {
        return AICAM_FALSE;
    }
    out->port = port;
    out->valid = AICAM_TRUE;
    return AICAM_TRUE;
}

static aicam_bool_t same_tuple_origin(const char *origin, const char *host_header)
{
    host_tuple_t o;
    host_tuple_t h;
    const char *auth;
    uint32_t default_port = 80;

    if (origin == NULL || host_header == NULL) {
        return AICAM_FALSE;
    }
    if (strncmp(origin, "http://", 7) == 0) {
        auth = origin + 7;
        default_port = 80;
    } else if (strncmp(origin, "https://", 8) == 0) {
        auth = origin + 8;
        default_port = 443;
    } else {
        return AICAM_FALSE;
    }
    if (auth[0] == '\0') {
        return AICAM_FALSE;
    }
    if (strchr(auth, '/') != NULL || strchr(auth, '?') != NULL ||
        strchr(auth, '#') != NULL || strchr(auth, '@') != NULL ||
        strchr(auth, '\\') != NULL || strchr(auth, ' ') != NULL) {
        return AICAM_FALSE;
    }
    if (!parse_authority(auth, &o, default_port)) {
        return AICAM_FALSE;
    }
    if (!parse_authority(host_header, &h, 80)) {
        return AICAM_FALSE;
    }
    if (strcmp(o.host, h.host) != 0 || o.port != h.port) {
        return AICAM_FALSE;
    }
    return AICAM_TRUE;
}

void web_host_policy_reset(void)
{
    uint32_t i;

    for (i = 0; i < WEB_HOST_POLICY_MAX_HOSTS; i++) {
        g_admin_hosts[i][0] = '\0';
    }
    g_admin_host_count = 0u;
    g_app_site_host[0] = '\0';
    g_app_site_explicit = AICAM_FALSE;
}

void web_host_policy_begin_admin_update(void)
{
    uint32_t i;

    for (i = 0; i < WEB_HOST_POLICY_MAX_HOSTS; i++) {
        g_admin_hosts[i][0] = '\0';
    }
    g_admin_host_count = 0u;
}

void web_host_policy_add_admin_host(const char *host)
{
    char normalized[WEB_HOST_POLICY_HOST_LEN];
    uint32_t i;

    normalize_host(host, normalized, sizeof(normalized));
    if (normalized[0] == '\0' ||
        g_admin_host_count >= WEB_HOST_POLICY_MAX_HOSTS) {
        return;
    }
    for (i = 0; i < g_admin_host_count; i++) {
        if (strcmp(g_admin_hosts[i], normalized) == 0) {
            return;
        }
    }
    (void)snprintf(g_admin_hosts[g_admin_host_count],
                   sizeof(g_admin_hosts[0]), "%s", normalized);
    g_admin_host_count++;
}

void web_host_policy_end_admin_update(const char *netif_hostname)
{
    if (g_app_site_explicit == AICAM_TRUE) {
        return;
    }
    g_app_site_host[0] = '\0';
    if (netif_hostname != NULL && netif_hostname[0] != '\0') {
        char derived[WEB_HOST_POLICY_HOST_LEN];
        (void)snprintf(derived, sizeof(derived),
                       WEB_HOST_POLICY_APP_SITE_PREFIX "%s", netif_hostname);
        normalize_host(derived, g_app_site_host, sizeof(g_app_site_host));
    }
}

void web_host_policy_set_app_site_host(const char *host)
{
    normalize_host(host, g_app_site_host, sizeof(g_app_site_host));
    g_app_site_explicit = AICAM_TRUE;
}

web_host_class_t web_host_policy_classify(const char *host_header)
{
    char normalized[WEB_HOST_POLICY_HOST_LEN];
    uint32_t i;

    normalize_host(host_header, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return WEB_HOST_UNKNOWN;
    }
    for (i = 0; i < g_admin_host_count; i++) {
        if (strcmp(g_admin_hosts[i], normalized) == 0) {
            return WEB_HOST_ADMIN;
        }
    }
    if (g_app_site_host[0] != '\0' &&
        strcmp(g_app_site_host, normalized) == 0) {
        return WEB_HOST_APP_SITE;
    }
    return WEB_HOST_UNKNOWN;
}

aicam_bool_t web_host_policy_origin_allowed(const char *origin, const char *host_header)
{
    if (origin == NULL || origin[0] == '\0' || strcmp(origin, "null") == 0) {
        return AICAM_FALSE;
    }
    if (web_host_policy_classify(host_header) != WEB_HOST_ADMIN) {
        return AICAM_FALSE;
    }
    return same_tuple_origin(origin, host_header);
}

static aicam_bool_t method_is_safe(const char *method)
{
    return strcmp(method, "GET") == 0 || strcmp(method, "HEAD") == 0 ||
           strcmp(method, "OPTIONS") == 0;
}

static aicam_bool_t uri_is_side_effect_get(const char *uri)
{
    static const char toggle[] = "/api/v1/ai/toggle";
    size_t n = sizeof(toggle) - 1u;

    return strncmp(uri, toggle, n) == 0 && (uri[n] == '\0' || uri[n] == '?');
}

web_entry_verdict_t web_host_policy_entry(const char *host_header,
                                          const char *uri,
                                          const char *method,
                                          const char *origin_header,
                                          aicam_bool_t origin_present)
{
    web_host_class_t cls;

    if (method == NULL || uri == NULL) {
        return WEB_ENTRY_CROSS_SITE;
    }
    cls = web_host_policy_classify(host_header);
    if (cls == WEB_HOST_UNKNOWN) {
        return WEB_ENTRY_UNKNOWN_HOST;
    }
    if (cls == WEB_HOST_APP_SITE) {
        if (strncmp(uri, "/appsite/", 9) != 0) {
            return WEB_ENTRY_APP_HOST_API;
        }
        if (!method_is_safe(method)) {
            return WEB_ENTRY_APP_HOST_API;
        }
        return WEB_ENTRY_ALLOW;
    }
    if (strcmp(method, "OPTIONS") == 0) {
        return WEB_ENTRY_ALLOW;
    }
    if (method_is_safe(method) && !uri_is_side_effect_get(uri)) {
        return WEB_ENTRY_ALLOW;
    }
    if (origin_present == AICAM_TRUE &&
        origin_header != NULL && origin_header[0] != '\0' &&
        strcmp(origin_header, "null") != 0) {
        return web_host_policy_origin_allowed(origin_header, host_header)
                   ? WEB_ENTRY_ALLOW
                   : WEB_ENTRY_CROSS_SITE;
    }
    return WEB_ENTRY_CROSS_SITE;
}
