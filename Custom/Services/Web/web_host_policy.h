#ifndef WEB_HOST_POLICY_H
#define WEB_HOST_POLICY_H

#include <stdint.h>
#include "aicam_types.h"

#define WEB_HOST_POLICY_MAX_HOSTS 8
#define WEB_HOST_POLICY_HOST_LEN 64
#define WEB_HOST_POLICY_APP_SITE_PREFIX "app."

typedef enum {
    WEB_HOST_UNKNOWN = 0,
    WEB_HOST_ADMIN,
    WEB_HOST_APP_SITE,
} web_host_class_t;

typedef enum {
    WEB_ENTRY_ALLOW = 0,
    WEB_ENTRY_UNKNOWN_HOST,
    WEB_ENTRY_APP_HOST_API,
    WEB_ENTRY_CROSS_SITE,
} web_entry_verdict_t;

typedef enum {
    WEB_LISTENER_ADMIN = 0,
    WEB_LISTENER_APP,
    WEB_LISTENER_UNKNOWN,
} web_listener_role_t;

void web_host_policy_reset(void);
void web_host_policy_begin_admin_update(void);
void web_host_policy_add_admin_host(const char *host);
void web_host_policy_end_admin_update(const char *netif_hostname);
void web_host_policy_set_app_site_host(const char *host);
void web_host_policy_set_app_site_authority(const char *authority);
web_host_class_t web_host_policy_classify(const char *host_header);
aicam_bool_t web_host_policy_origin_allowed(const char *origin, const char *host_header);
web_entry_verdict_t web_host_policy_entry(const char *host_header,
                                          const char *uri,
                                          const char *method,
                                          const char *origin_header,
                                          aicam_bool_t origin_present);
web_entry_verdict_t web_host_policy_entry_role(const char *host_header,
                                               const char *uri,
                                               const char *method,
                                               const char *origin_header,
                                               aicam_bool_t origin_present,
                                               web_listener_role_t role);

#endif
