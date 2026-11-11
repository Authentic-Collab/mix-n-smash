#pragma once

#include <stddef.h>
#include "settings.h"

/**
 * Bring up Wi-Fi: join the configured network, or start an access point
 * when none is configured or joining keeps failing. Also announces the
 * hostname over mDNS. Does nothing when Wi-Fi is disabled in menuconfig.
 */
void net_init(const mns_settings_t *s);

/** For the settings page: "joined 'home'" / "access point 'mix-n-smash'", and the IP. */
void net_status(char *network, size_t network_cap, char *ip, size_t ip_cap);
