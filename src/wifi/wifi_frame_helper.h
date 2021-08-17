#ifndef _WIFI_FRAME_HELPER_H_
#define _WIFI_FRAME_HELPER_H_

struct ieee80211_hdr {
        uint16_t frame_control;
        uint16_t duration;
        uint8_t dst_mac[6];
        uint8_t sar_mac[6];
        uint8_t bssid[6];
        uint16_t seq_ctrl;
} __attribute__((__packed__));

struct ieee80211_mgmt {
	uint16_t frame_control;
	uint16_t duration;
	uint8_t dst_mac[6];
	uint8_t sar_mac[6];
	uint8_t bssid[6];
	uint16_t seq_ctrl;
	union {
		struct {
			uint64_t timestamp;
			uint16_t beacon_int;
			uint16_t capab_info;
			uint8_t var[0];
			} __attribute__((__packed__)) beacon;
		struct {
			uint8_t category;
			uint8_t oui[3];
			uint8_t var[0];
			} __attribute__((__packed__)) vendor_action;
	};
} __attribute__((__packed__));
#endif
