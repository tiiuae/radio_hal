#ifndef __WIFI_HAL_H__
#define __WIFI_HAL_H__

#include <string>
#include "radio_hal.h"

enum wifi_state {
	IF_DOWN_STATE,
	IF_UP_STATE,
	ASSOCIATED_STATE,
	CONNECTED_STATE,
	DISCONNECTED_STATE,
};

struct netlink_ctx {
	struct nl_sock *sock;
	int nl80211_id;
	int ifindex;
};

struct wifi_sotftc {
	unsigned char mac_addr[6];
	struct netlink_ctx ctx;
	char ifname[30];
	int ifindex;
	int signal;
	int txrate;
	int rxrate;
	int rssi;
	int avg_rssi;
	int channel;
	int mcs;
	enum wifi_state state;
};

int wifi_hal_attach(struct radio_context *ctx);
int wifi_hal_dettach(struct radio_context *ctx);

#endif
