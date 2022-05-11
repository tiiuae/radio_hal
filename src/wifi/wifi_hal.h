#ifndef __WIFI_HAL_H__
#define __WIFI_HAL_H__

enum wifi_state {
	UNKNOWN_STATE = -1,
	INIT_STATE,
	IF_DOWN_STATE,
	IF_UP_STATE,
	ASSOCIATED_STATE,
	CONNECTED_STATE,
	DISCONNECTED_STATE,
	LAST_STATE  /* Don't remove */
};

//Events
enum wifi_SystemEvent {
	NO_EVENT = -1,
	STARTUP_EVENT,
	AP_ENABLED_EVENT,
	MESH_GROUP_STARTED_EVENT,
	DISCONNECTED_EVENT,
	CONNECTED_EVENT,
	TERMINATE_EVENT,
	LAST_EVENT
};

//typedef of function pointer
typedef wifi_state (*wifiEventHandler)(struct radio_context *ctx);

//structure of state and event with event handler
typedef struct {
	wifi_state StateMachine;
	wifi_SystemEvent StateMachineEvent;
	wifiEventHandler StateMachineEventHandler;
} wifi_StateMachine;

enum radio {
	WIFI_RADIO_0,
	WIFI_RADIO_1,
	WIFI_RADIO_MAX  /* Don't remove */
};

struct netlink_ctx {
	struct nl_sock *sock;
	int nl80211_id;
	int ifindex;
	char ifname[RADIO_IFNAME_SIZE];
	char phyname[RADIO_PHYNAME_SIZE];
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
