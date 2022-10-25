#ifndef __WIFI_HAL_H__
#define __WIFI_HAL_H__

#include <string>
#include "radio_hal.h"
#define RADIO_DEBUGFS_DIRSIZE 264

enum wifi_state {
	UNKNOWN_STATE = -1,
	INIT_STATE,
	IF_DOWN_STATE,
	IF_INIT_STAGE_1,
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
	STARTUP_STAGE_2_EVENT,
	AP_ENABLED_EVENT,
	MESH_GROUP_STARTED_EVENT,
	DISCONNECTED_EVENT,
	CONNECTED_EVENT,
	TERMINATE_EVENT,
	LAST_EVENT
};

enum wifi_driver_version {
	WIFI_DRIVER_ATH9K,
	WIFI_DRIVER_ATH10K,
	WIFI_DRIVER_ATH11K,
	WIFI_DRIVER_BRCM_FMAC,
	WIFI_DRIVER_VERSION_MAX,
};

//typedef of function pointer
typedef wifi_state (*wifiEventHandler)(struct radio_context *ctx, int index);

//structure of state and event with event handler
typedef struct {
	wifi_state StateMachine;
	wifi_SystemEvent StateMachineEvent;
	wifiEventHandler StateMachineEventHandler;
} wifi_StateMachine;

enum radio {
	WIFI_RADIO_0,
	WIFI_RADIO_1,   /* Enable this, if you have two wifi cards */
	WIFI_RADIO_MAX  /* Don't remove */
};

struct netlink_ctx {
	struct nl_sock *sock;
	int nl80211_id;
	int ifindex[WIFI_RADIO_MAX];
	char ifname[WIFI_RADIO_MAX][RADIO_IFNAME_SIZE];
	char phyname[WIFI_RADIO_MAX][RADIO_PHYNAME_SIZE];
	char debugfs_root[WIFI_RADIO_MAX][RADIO_DEBUGFS_DIRSIZE];
	struct nl_cb *if_cb;
	struct nl_cb *link_info_cb;
	struct nl_cb *set_cb;
	struct nl_cb *gen_nl_cb;
	int if_cb_err;
	int set_cb_err;
	int csa_cb_err;
	int linkinfo_cb_err;
	enum wifi_driver_version drv_version;
};

struct wpa_ctrl_ctx {
	int fd;
	struct wpa_ctrl *ctrl;
	struct wpa_ctrl *monitor;
	struct wpa_ctrl *mesh_ctrl;
};

struct wifi_softc {
	char mac_addr[WIFI_RADIO_MAX][RADIO_MACADDR_SIZE];
	struct netlink_ctx nl_ctx;
	struct wpa_ctrl_ctx wpa_ctx[WIFI_RADIO_MAX];
	int signal[WIFI_RADIO_MAX];
	int txrate[WIFI_RADIO_MAX];
	int rxrate[WIFI_RADIO_MAX];
	int rssi[WIFI_RADIO_MAX];
	int avg_rssi[WIFI_RADIO_MAX];
	int channel[WIFI_RADIO_MAX];
	int mcs[WIFI_RADIO_MAX];
	enum wifi_state state;
	int radio_amount;
};

struct radio_context* wifi_hal_attach();
int wifi_hal_dettach(struct radio_context *ctx);
int wifi_debugfs_init(struct wifi_softc *sc, int index);
int wifi_debugfs_read(struct wifi_softc *sc, const char *filename, char *buf, int buf_size, int index);
int wifi_debugfs_write(struct wifi_softc *sc, const char *filename, const char *cmd, int index);
int wifi_debugfs_search(struct wifi_softc *sc, const char *filename, const char *substring, int index);
int wifi_get_fw_stats(struct wifi_softc *sc, char *buf, int buf_size, int index);
int wifi_capture_spectral_scan(struct wifi_softc *sc, int index);
int wifi_hal_trigger_scan(struct wifi_softc *sc, int index);
#endif
