#include <getopt.h>
#include <cstring>
#include <cstdlib>
#include "radio_hal.h"
#include "debug.h"
#include "radio_hal_yaml.h"


#ifdef RADIO_HAL_UNIT_TEST
static int test_radio_hal_api(struct radio_context *ctx, char *argv[],
		enum radio_type type)
{
	int err = 0;
	char version[32] = {0};
	char ifname[RADIO_IFNAME_SIZE] = {0};
	char mac_addr[RADIO_MACADDR_SIZE] = {0};
	char scan_results[4096] = {0};
	struct radio_generic_func *radio_ops = ctx->cmn.rd_func;
	char *cmd = argv[2];
	struct wifi_config *w_config;
	struct modem_config *m_config;

	switch(type)
	{
		case RADIO_WIFI:
			w_config = (struct wifi_config *)malloc(sizeof(wifi_config));
			strncpy(w_config->ssid, argv[3],sizeof(w_config->ssid)-1);
			strncpy(w_config->key, argv[4],sizeof(w_config->key)-1);
			strncpy(w_config->freq, argv[5],sizeof(w_config->freq)-1);

			if(!strcmp(cmd, "radio_get_hal_version")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_hal_version(version);
				hal_info(HAL_DBG_WIFI, "VERSION:%s\n", (char*) &version);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_iface_name")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_iface_name(ctx, ifname, 1);
				hal_info(HAL_DBG_WIFI, "IFNAME:%s\n", (char*) &ifname);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_rssi")) {
				radio_ops->open(ctx, RADIO_WIFI);
				hal_info(HAL_DBG_WIFI, "RSSI:%d dbm\n", radio_ops->radio_get_rssi(ctx, 1));
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_txrate")) {
				radio_ops->open(ctx, RADIO_WIFI);
				hal_info(HAL_DBG_WIFI, "TXRATE:%d MBit/s\n", radio_ops->radio_get_txrate(ctx, 1));
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_rxrate")) {
				radio_ops->open(ctx, RADIO_WIFI);
				hal_info(HAL_DBG_WIFI, "RXRATE:%d MBit/s\n", radio_ops->radio_get_rxrate(ctx, 1));
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_macaddr")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_mac_address(ctx, mac_addr, 1);
				hal_info(HAL_DBG_WIFI, "MACADDR:%s \n", mac_addr);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_get_scan_result")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_scan_results(ctx, scan_results);
				hal_info(HAL_DBG_WIFI, "%s\n", scan_results);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_connect_ap")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_scan_results(ctx, scan_results);
				hal_info(HAL_DBG_WIFI, "%s\n", scan_results);
				radio_ops->radio_connect_ap(ctx);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_hal_create_ap")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_create_ap(ctx);
				radio_ops->close(ctx, RADIO_WIFI);
			} else if(!strcmp(cmd, "radio_mesh_join")) {
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_iface_name(ctx, ifname, 1);
				radio_ops->radio_join_mesh(ctx);
				radio_ops->close(ctx, RADIO_WIFI);
			}
			break;
		case RADIO_BT:
			break;
		case RADIO_15_4:
			break;
		case RADIO_MODEM:
			m_config = (struct modem_config *)malloc(sizeof(modem_config));
			strncpy(m_config->at_serial, argv[5],sizeof(m_config->at_serial)-1);

			if(!strcmp(cmd, "radio_get_hal_version")) {
				radio_ops->open(ctx, RADIO_MODEM);
				radio_ops->radio_get_hal_version(version);
				hal_info(HAL_DBG_MODEM, "VERSION:%s\n", version);
			} else if(!strcmp(cmd, "radio_hal_connect")) {
				radio_ops->open(ctx, RADIO_MODEM);
				radio_ops->radio_connect(ctx);
				radio_ops->close(ctx, RADIO_MODEM);
			} else if(!strcmp(cmd, "radio_hal_get_rssi")) {
				radio_ops->open(ctx, RADIO_MODEM);
				err = radio_ops->radio_connect(ctx);
				if (!err)
					hal_info(HAL_DBG_MODEM, "RSSI:%d dbm\n", radio_ops->radio_get_rssi(ctx, 1));
				/* commented to keep modem alive, otherwise turns modem off */
				//radio_ops->close(ctx, RADIO_MODEM);
			}
			break;
	}

	return err;
}

static void show_radio_hal_help()
{
	printf("\n------------------------- Radio HAL uses --------------------------------\n");
	printf("./radio_hal_daemon -w <radio index> Attach wifi radio HAL \n");
	printf("./radio_hal_daemon -b <radio index> Attach BT radio HAL \n");
	printf("./radio_hal_daemon -z <radio index> Attach 15.4 radio HAL \n");
	printf("./radio_hal_daemon -m <radio index> Attach Modem radio HAL \n");
	printf("\n-------------------------- ----------------------------------------------\n");
}

int main(int argc, char *argv[])
{
	int c;
	const char *short_opt = "w::b::z::m::h::";
	int long_opt_ptr;
	int err = 0;
	struct radio_context *ctx = nullptr;
	struct option long_opt[] =
	{
		{"wifi", required_argument,nullptr, 'w'},
		{"bt", required_argument,nullptr, 'b'},
		{"Zigbee", required_argument,nullptr, 'z'},
		{"Modem", required_argument,nullptr, 'm'},
		{"help", optional_argument,nullptr, 'h'}
	};

	printf("*  argc = %d argv = %s %s\n",argc,argv[0], argv[2]);
	while((c = getopt_long(argc, argv, short_opt, long_opt, &long_opt_ptr)) != -1)
	{

		switch(c)
		{
			case 'w':
				ctx = radio_hal_attach(RADIO_WIFI);
				if (!ctx)
					hal_err(HAL_DBG_WIFI, "failed to attach Wifi Radio HAL\n");
				if (argc >= 3)
					err = test_radio_hal_api(ctx, argv, RADIO_WIFI);
				radio_hal_dettach(ctx, RADIO_WIFI);
				break;
			case 'b':
				ctx = radio_hal_attach(RADIO_BT);
				if (!ctx)
					hal_err(HAL_DBG_BT, "failed to attach BT Radio HAL\n");
				break;
			case 'z':
				ctx = radio_hal_attach(RADIO_15_4);
				if (!ctx)
					hal_err(HAL_DBG_BT, "failed to attach 15.4 Radio HAL\n");
				break;
			case 'm':
				ctx = radio_hal_attach(RADIO_MODEM);
				if (!ctx)
					hal_err(HAL_DBG_MODEM, "failed to attach Modem Radio HAL\n");
				if (argc >= 3)
					err = test_radio_hal_api(ctx, argv, RADIO_MODEM);
				radio_hal_dettach(ctx, RADIO_MODEM);
				break;
			case 'h':
				show_radio_hal_help();
				return(0);
			default:
				hal_err(HAL_DBG_COMMON, "Argument not supported: %c\n", c);
				return(0);
		}
	}

	return err;
}
#endif