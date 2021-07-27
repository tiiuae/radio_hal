#ifndef __WIFI_HAL_H__
#define __WIFI_HAL_H__

#include <string>
#include "radio_hal.h"

struct netlink_ctx {
	struct nl_sock *sock;
	int nl80211_id;
	int ifindex;
};

int wifi_hal_attach(struct radio_context *ctx);
int wifi_hal_dettach(struct radio_context *ctx);

#endif
