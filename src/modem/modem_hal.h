#ifndef __MODEM_HAL_H__
#define __MODEM_HAL_H__



struct modem_softc {
	int atif;
	char modem[20];
	char wwan[20];
};

typedef enum {
	SIM_ABSENT = 0,
	SIM_NOT_READY = 1,
	SIM_READY = 2,
	SIM_PIN = 3,
	SIM_PUK = 4,
	SIM_NETWORK_PERSONALIZATION = 5,
	RUIM_ABSENT = 6,
	RUIM_NOT_READY = 7,
	RUIM_READY = 8,
	RUIM_PIN = 9,
	RUIM_PUK = 10,
	RUIM_NETWORK_PERSONALIZATION = 11,
	ISIM_ABSENT = 12,
	ISIM_NOT_READY = 13,
	ISIM_READY = 14,
	ISIM_PIN = 15,
	ISIM_PUK = 16,
	ISIM_NETWORK_PERSONALIZATION = 17,
} SIM_Status;


struct radio_context* modem_hal_attach();
int modem_hal_detach(struct radio_context *ctx);

#endif
