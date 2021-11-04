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
	struct nl_cb *gen_nl_cb;
	int if_cb_err;
	int csa_cb_err;
	int linkinfo_cb_err;
};

struct wpa_ctrl_ctx {
	int fd;
	struct wpa_ctrl *ctrl;
	struct wpa_ctrl *monitor;
	struct wpa_ctrl *mesh_ctrl;
};

struct wifi_softc {
	char mac_addr[RADIO_MACADDR_SIZE];
	struct netlink_ctx nl_ctx;
	struct wpa_ctrl_ctx wpa_ctx;
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
