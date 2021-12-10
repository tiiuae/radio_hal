#include <unistd.h>
#include "radio_hal.h"
#include <netlink/handlers.h>
#include <netlink/netlink.h>

#define MODEM_RADIO_HAL_MAJOR_VERSION 0
#define MODEM_RADIO_HAL_MINOR_VERSION 1

#define CMD_BUFFER_SIZE sizeof(char) * 2048
#define RESP_BUFFER_SIZE sizeof(char) * 2048

static int debug;

__attribute__((unused)) int modem_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size)
{
	FILE *f;
	char *buf = resp_buf;
	int size=resp_size, resp_buf_bytes, readbytes;
    char *ret;

	if((f = popen(cmd, "r")) == nullptr) {
		printf("popen %s error\n", cmd);
		return -1;
	}

	while(!feof(f))
	{
		*buf = 0;
		if(size>=128) {
			resp_buf_bytes=128;
		} else {
			resp_buf_bytes=size-1;
		}

		ret = fgets(buf,resp_buf_bytes,f);
		readbytes = (int)strlen(buf);
		if (!readbytes || ret == nullptr)
			break;

		size -= readbytes;
		buf += readbytes;

	}
	pclose(f);
	resp_buf[resp_size-1]=0;

	if (debug)
		printf("sys cmd:%s resp:%s\n", cmd, resp_buf);

	return 0;
}

__attribute__((unused)) static int replace_line_change(char *buf, size_t size) {
	int len;

	len = strnlen(buf, size);
	if (len > 0 && (buf[len-1] == '\n'|| buf[len-1] == '\0') ) {
		buf[len-1] = '\0';
	} else {
		printf("fail to find line change \n");
		return -1;
	}
	return 0;
}

static int modem_hal_get_hal_version(char *version)
{
	snprintf(version, 32, "%d.%d", MODEM_RADIO_HAL_MAJOR_VERSION, MODEM_RADIO_HAL_MINOR_VERSION);

	return 0;
}

static int modem_hal_open(struct radio_context *ctx, enum radio_type type)
{
	return 0;
}

static int modem_hal_close(struct radio_context *ctx, enum radio_type type)
{
	return 0;
}

static struct radio_generic_func modem_hal_ops = {
	.open = modem_hal_open,
	.close = modem_hal_close,
	.radio_get_hal_version = modem_hal_get_hal_version,
	.radio_initialize = nullptr,
	.radio_wait_for_driver_ready = nullptr,
	.radio_cleanup = nullptr,
	.radio_event_loop = nullptr,
	.radio_create_config = nullptr,
	.radio_enable = nullptr,
	.get_no_of_radio = nullptr,
	.radio_get_iface_name = nullptr,
	.radio_get_supported_freq_band = nullptr,
	.radio_get_status = nullptr,
	.radio_get_feature_status = nullptr,
	.radio_get_supported_channels = nullptr,
	.radio_get_operating_channel = nullptr,
	.radio_get_mac_address = nullptr,
	.radio_get_rssi = nullptr,
	.radio_get_txrate = nullptr,
	.radio_get_rxrate = nullptr,
	.radio_get_scan_results = nullptr,
	.radio_connect_ap = nullptr,
	.radio_create_ap = nullptr,
	.radio_join_mesh = nullptr,
};

__attribute__((unused)) int modem_hal_register_ops(struct radio_context *ctx)
{
	ctx->cmn.rd_func = &modem_hal_ops;

	return 0;
}

struct radio_context*  modem_hal_attach()
{
	struct radio_context *ctx = nullptr;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		printf("failed to allocate radio hal ctx\n");
		return nullptr;
	}

	/* need register somewhere TODO */

	ctx->cmn.rd_func = &modem_hal_ops;
	printf("Modem HAL attach completed\n");

	return ctx;
}

int modem_hal_detach(struct radio_context *ctx)
{
	free(ctx);

	printf("MODEM HAL detach completed\n");
	return 0;
}
