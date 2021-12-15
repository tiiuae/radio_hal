#ifndef __MODEM_HAL_H__
#define __MODEM_HAL_H__



struct modem_softc {
	int atif;
	char modem[20];
	char wwan[20];
};

struct radio_context* modem_hal_attach();
int modem_hal_detach(struct radio_context *ctx);

#endif
