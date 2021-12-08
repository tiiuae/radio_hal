#include <cstdio>
#include <getopt.h>
#include "radio_hal.h"
#include "wifi_hal.h"
#include "radio_hal_yaml.h"

#ifndef RADIO_HAL_UNIT_TEST
static void show_radio_hal_help()
{
	printf("\n------------------------- Radio HAL uses --------------------------------\n");
	printf("./radio_hal_daemon -w <yaml configuration file wifi>\n");
	printf("./radio_hal_daemon -b <yaml configuration file bluetooth>\n");
	printf("./radio_hal_daemon -z <yaml configuration file 15.4 radio>\n");
	printf("\n-------------------------- ----------------------------------------------\n");
}

int main(int argc, char *argv[]) {
	int c, ret = 0;
	const char *short_opt = "w::b::z::h::";
	int long_opt_ptr;
	struct radio_context *ctx = nullptr;
	char ifname[RADIO_IFNAME_SIZE] = {0};
	void *config = nullptr;
	struct wifi_config *w_config = nullptr;
	struct radio_generic_func *radio_ops;
	struct option long_opt[] =
			{
					{"wifi",   required_argument, nullptr, 'w'},
					{"bt",     required_argument, nullptr, 'b'},
					{"Zigbee", required_argument, nullptr, 'z'},
					{"help",   optional_argument, nullptr, 'h'}
			};


	printf("*  argc = %d argv = %s %s\n", argc, argv[0], argv[2]);
	while ((c = getopt_long(argc, argv, short_opt, long_opt, &long_opt_ptr)) != -1) {
		switch (c) {
			case 'w':
				ctx = radio_hal_attach(RADIO_WIFI);
				if (!ctx || !ctx->cmn.rd_func) {
					printf("failed to attach Wifi Radio HAL\n");
					return -1;
				} else
					radio_ops = ctx->cmn.rd_func;

				config = (wifi_config *)malloc(sizeof(wifi_config));
				if (argc >= 3)
					ret = radio_hal_yaml_config(config, (char *)argv[2], RADIO_WIFI);
				if (ret<0)
					return -1;

				w_config = (struct wifi_config *) config;
				/* e.g. mesh start */
				radio_ops->open(ctx, RADIO_WIFI);
				radio_ops->radio_get_iface_name(ctx, ifname, 1);
				radio_ops->radio_join_mesh(ctx, w_config->ssid, w_config->key, w_config->freq);
				// TODO TBD status communication
				//radio_ops->close(ctx, RADIO_WIFI);
				//free(config);
				//radio_hal_dettach(ctx, RADIO_WIFI);
				break;
			case 'b':
				ctx = radio_hal_attach(RADIO_BT);
				if (!ctx)
					printf("failed to attach BT Radio HAL\n");
				config = (bt_config *)malloc(sizeof(bt_config));
				ret = radio_hal_yaml_config(config, (char *)argv[2], RADIO_BT);
				break;
			case 'z':
				ctx = radio_hal_attach(RADIO_15_4);
				if (!ctx)
					printf("failed to attach 15.4 Radio HAL\n");
				config = (z_config *)malloc(sizeof(z_config));
				ret = radio_hal_yaml_config(config, (char *)argv[2], RADIO_15_4);
				break;
			case 'h':
				show_radio_hal_help();
				return (0);
			default:
				printf("Argument not supported: %c\n", c);
				return (0);
		}
	}
	return 0;
}
#endif