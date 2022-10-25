#ifndef RADIO_HAL_RADIO_HAL_YAML_H
#define RADIO_HAL_RADIO_HAL_YAML_H

struct wifi_config {
	bool debug;
	int api_version;		// interface version for future purposes
	int phy;				// phy number
	char key[64];			// key for the network
	char passphrase[64];
	char ssid[32];			// 0-32 octets, UTF-8
	char enc[8];			// encryption (none wep, wpa2, wpa3, sae)
	char ap_mac_addr[RADIO_MACADDR_SIZE];
	char country[3];		// Country code, sets tx power limits and supported channels
	char freq[6];
	char bw[4];				// 5/10/20/??
	char preamble[6];		// short/long
	int distance;			// distance (coverage-class fine tuning)
	int tx_power;			/* select 30dBm, HW and regulations limiting it correct level.
							* Can be used to set lower dBm levels for testing purposes (e.g. 5dBm) */
	char mode[5];			// mesh=mesh network, ap=debug hotspot sta=connect ap
	char type[5];			// 11s or ibss
	int mesh_fwding;		// distance (coverage-class fine tuning)
};

struct bt_config {
	bool debug;
};

struct z_config {
	bool debug;
};

struct modem_config {
	char apn[20];			// internet APN
	char pin[5];			// SIM pin number
	char at_serial[20];	    // e.g. /dev/ttyUSB2, /dev/ttyACM4
};


int radio_hal_yaml_config(void *conf_struct, char* yaml_file, radio_type radio);

#endif //RADIO_HAL_RADIO_HAL_YAML_H
