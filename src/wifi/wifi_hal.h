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
	char ifname[RADIO_IFNAME_SIZE];
	struct nl_cb *if_cb;
	struct nl_cb *link_info_cb;
	int if_cb_err;
	int linkinfo_cb_err;
};

struct wifi_sotftc {
	unsigned char mac_addr[6];
	struct netlink_ctx nl_ctx;
	int signal;
	int txrate;
	int rxrate;
	int rssi;
	int avg_rssi;
	int channel;
	int mcs;
	enum wifi_state state;
};

struct radio_context* wifi_hal_attach();
int wifi_hal_dettach(struct radio_context *ctx);

#endif
