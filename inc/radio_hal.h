#ifndef __RADIO_HAL_H__
#define __RADIO_HAL_H__

enum radio_state {
	RADIO_IDLE_STATE,
	RADIO_SLEEP_STATE,
	RADIO_ACTIVE_STATE,
	RADIO_STOP_STATE,
	RADIO_MAX_STATE,
};

enum radio_type {
	RADIO_WIFI,
	RADIO_BT,
	RADIO_ZIGBEE,
	RADIO_LORA,
	RADIO_MAX,
};

struct radio_common
{
	const char *radio_name;
	enum radio_type type;
	int hal_version;
	struct radio_generic_func;
};

typedef struct radio_context
{
	struct radio_common cmn;
	void *radio_private;
};

typedef struct radio_generic_func {
    int (*open)(struct radio_context *ctx, enum radio_type type);
	int (*close)(struct radio_context *ctx, enum radio_type type);
	int (*radio_get_hal_version)(int *version);
} radio_gen_func_t;

int radio_hal_attach(radio_gen_func_t *func, enum radio_type type);
int radio_hal_dettach(radio_gen_func_t *func, enum radio_type type);

#endif
