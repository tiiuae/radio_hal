#include <cstdio>
#include <getopt.h>
#include "radio_hal.h"
#include "wifi_hal.h"
#include "modem_hal.h"
#include "radio_hal_yaml.h"

struct radio_context* radio_hal_attach(enum radio_type type)
{
	struct radio_context *ctx = nullptr;
	/* int err = 0; TODO */

	switch(type)
	{
		case RADIO_WIFI:
			ctx = wifi_hal_attach();
			break;
		case RADIO_BT:
			break;
		case RADIO_15_4:
			break;
		case RADIO_MODEM:
			ctx = modem_hal_attach();
			break;
	}

	return ctx;
}

int radio_hal_dettach(struct radio_context *ctx, enum radio_type type)
{
	int err = 0;

	switch(type)
	{
		case RADIO_WIFI:
			err = wifi_hal_dettach(ctx);
			break;
		case RADIO_BT:
			break;
		case RADIO_15_4:
			break;
		case RADIO_MODEM:
			err = modem_hal_detach(ctx);
			break;
	}

	return err;
}
