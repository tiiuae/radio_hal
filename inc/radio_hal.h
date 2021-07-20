#ifndef __RADIO_HAL_H__
#define __RADIO_HAL_H__

#define RADIO_IFNAME_SIZE 16

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

enum radio_feature {
	RADIO_FEATURE_INFRA,
	RADIO_FEATURE_MESH,
	RADIO_FEATURE_ADHOC,
	RADIO_FEATURE_P2P,
	RADIO_FEATURE_STATS,
	RADIO_FEATURE_RADIO_MEASUREMENT,
	RADIO_FEATURE_CSI_CAPTURE,
	RADIO_FEATURE_MAX,
};

enum radio_freq_band {
	RADIO_2_4_BAND = 1 << 0,
	RADIO_5_0_BAND = 1 << 1,
	RADIO_6_0_BAND = 1 << 2,
};

typedef enum {
	RADIO_CHAN_WIDTH_20	= 0,
	RADIO_CHAN_WIDTH_40	= 1,
	RADIO_CHAN_WIDTH_80	= 2,
	RADIO_CHAN_WIDTH_160   = 3,
	RADIO_CHAN_WIDTH_80P80 = 4,
	RADIO_CHAN_WIDTH_5	 = 5,
	RADIO_CHAN_WIDTH_10	= 6,
	RADIO_CHAN_WIDTH_INVALID = -1
} radio_channel_width;

typedef struct {
    char iface_name[RADIO_IFNAME_SIZE + 1];
    int channel;
} radio_iface_info;

struct {
    radio_channel_width width;
    int center_frequency0;
    int center_frequency1;
    int primary_frequency;
} radio_channel_spec;

typedef struct radio_generic_func {
	int (*open)(struct radio_context *ctx, enum radio_type type);
	int (*close)(struct radio_context *ctx, enum radio_type type);
	int (*radio_get_hal_version)(int *version);
	int (* radio_initialize) (struct radio_context *ctx);
	int (* radio_wait_for_driver_ready) (struct radio_context *ctx);
	void (* radio_cleanup) (struct radio_context *ctx);
	void (*radio_event_loop)(struct radio_context *ctx);
	int (*radio_create_config)(void);
	int (*radio_enable) (struct radio_context *ctx, int radio_index, unsigned char  enable);
	int (*get_no_of_radio)(unsigned long *no, enum radio_type type);
	int (* radio_get_iface_name) (struct radio_context *ctx, char *name, int radio_index);
	enum radio_freq_band (*radio_get_supported_freq_band) (struct radio_context *ctx, int radio_index);
	int (*radio_get_status) (struct radio_context *ctx, int radio_index);
	int (* radio_get_feature_status) (struct radio_context *ctx, enum radio_feature);
	int (* radio_get_supported_channels)(struct radio_context *ctx, struct radio_channel_spec *ch_list, int *no_ch);
	int (* radio_get_operating_channel) (struct radio_context *ctx, int radio_index);
	int (* radio_get_mac_address) (struct radio_context *ctx, int radio_index);

} radio_gen_func_t;

struct radio_common
{
	const char *radio_name;
	enum radio_type type;
	int hal_version;
	unsigned char mac_addr[6];
	struct radio_generic_func rd_func;
};

struct radio_context
{
	struct radio_common cmn;
	void *radio_private;
};

int radio_hal_attach(radio_gen_func_t *func, enum radio_type type);
int radio_hal_dettach(radio_gen_func_t *func, enum radio_type type);

#endif
