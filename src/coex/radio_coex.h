#ifndef __RADIO_COEX_H__
#define __RADIO_COEX_H__

#define BIT_SET(value,bitno)    ( (value) |= (1<<(bitno)))
#define BIT_CLEAR(value,bitno)  ( (value) &= ~(1<<(bitno)))

enum radio_coex_status {
	COEX_WIFI_IDLE,
	COEX_WIFI_ACTIVE,
	COEX_SF_IDLE,
	COEX_SF_ACTIVE,
	COEX_BT_IDLE,
	COEX_BT_ACTIVE,
	COEX_MODEM_IDLE,
	COEX_MODEM_ACTIVE,
	COEX_STATUS_MAX,
};

struct radio_coex_context {
	int coex_status;
};

void radio_set_coex_status(struct radio_coex_context *ctx, enum radio_coex_status status);
void radio_clear_coex_status(struct radio_coex_context *ctx, enum radio_coex_status status);
int radio_get_coex_status(struct radio_coex_context *ctx);
#endif
