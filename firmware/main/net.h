#pragma once

/**
 * Bring up Wi-Fi: join the configured network, or start an access point
 * when none is configured or joining keeps failing. Also announces the
 * hostname over mDNS. Does nothing when Wi-Fi is disabled in menuconfig.
 */
void net_init(void);
