#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <netlink/netlink.h>
#include "radio_hal.h"
#include "modem_hal.h"

#define MODEM_RADIO_HAL_MAJOR_VERSION 0
#define MODEM_RADIO_HAL_MINOR_VERSION 1

static int debug;

static int modem_hal_get_hal_version(char *version)
{
	snprintf(version, 32, "%d.%d", MODEM_RADIO_HAL_MAJOR_VERSION, MODEM_RADIO_HAL_MINOR_VERSION);
	return 0;
}

static int replace_line_change(char *buf, size_t size) {
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

static int write_to_serial(struct modem_softc *sc, const char *cmd)
{
	int err = 0;

	if(!cmd) {
		return -1;
	}

	err = write(sc->atif, cmd, (size_t)strlen(cmd));

	if (!err) //TODO
		return -1;

	return 0;
}

__attribute__((unused)) static int read_from_serial(struct modem_softc *sc, char *response, int resp_len)
{
	ssize_t err = 0;

	if(!response) {
		return -1;
	}

	err = read(sc->atif, response, resp_len - 1);
	printf("read_from_serial %ld %s", err, response);
	if (!err) //TODO
		return -1;

	return 0;
}

__attribute__((unused)) static int at_send_command(struct modem_softc *sc, const char *cmd, long timeout_msec, char *response, int len)
{
	int err = 0;

	if(!cmd) {
		return -1;
	}

	err =  write_to_serial(sc, cmd);
	if (err < 0) {
		return -1;
	}

	//sleep(timeout_msec/1000);
	//err =  read_from_serial(sc, response, len);

	if(debug)
		printf("at_send_command:%s resp:%s\n", cmd, response);

	if (err < 0) {
		return -1;
	}
	return err;
}

static int modem_hal_find_modem(struct modem_softc *sc, char *resp_buf, int len)
{
	int ret;

	ret = radio_hal_run_sys_cmd((char *)"ls /sys/class/usbmisc/ |grep cdc", resp_buf, len);
	if (ret) {
		printf("failed to get modem\n");
		return -1;
	}

	ret = replace_line_change(resp_buf, (size_t)len);
	if (ret<0) {
		printf("fail with modem\n");
		return -1;
	}

	if (debug)
		printf("modem |%s|\n", resp_buf);

	return 0;
}

static int modem_hal_find_wwan(struct modem_softc *sc, char *resp_buf, int len)
{
	int ret;
	char cmd[CMD_BUFFER_SIZE] = {0};

	radio_hal_prepare_cmd_buf(cmd, sizeof(cmd), (const char*) "qmicli --device=/dev/%s --get-wwan-iface", sc->modem);
	ret = radio_hal_run_sys_cmd(cmd, resp_buf, len);
	if (ret) {
		printf("failed to get wwan\n");
		return -1;
	}

	ret = replace_line_change(resp_buf, (size_t)len);
	if (ret<0) {
		printf("fail with wwan\n");
		return -1;
	}

	if (debug)
		printf("wwan |%s|\n", resp_buf);

	return 0;
}

static int modem_hal_serial_attach(struct modem_softc *sc)
{
		int ret = 0;
		char cmd_buf[CMD_BUFFER_SIZE] = {0};
		char resp_buf[RESP_BUFFER_SIZE] = {0};
		size_t len = sizeof(resp_buf) - 1;

		sc->atif = open("/dev/ttyUSB2", O_RDWR | O_NOCTTY | O_SYNC);
		if (!sc->atif) {
			printf("Failed to open %s!\n", "/dev/ttyUSB2");
			return -1;
		}

		/* TODO at command support */
		/* ret = at_send_command(sc, "ATATE0Q0V1", 1000, resp_buf, len);

		if (ret)
			return -1; */

		ret = modem_hal_find_modem(sc, resp_buf, len);
		if (ret) {
			printf("Failed modem_hal_get_modem()\n");
			return -1;
		}
		strcpy(sc->modem, resp_buf);

		ret = modem_hal_find_wwan(sc, resp_buf, len);
		if (ret) {
			printf("Failed modem_hal_get_wwan()\n");
			return -1;
		}
		strcpy(sc->wwan, resp_buf);

		radio_hal_prepare_cmd_buf(cmd_buf, sizeof(cmd_buf), (const char*) "echo Y > /sys/class/net/%s/qmi/raw_ip", sc->wwan);
		ret = radio_hal_run_sys_cmd(cmd_buf, resp_buf, (int)len);
		if (ret) {
			printf("Failed to turn on wwan raw_ip mode\n");
			return -1;
		}

        return 0;
}

static int modem_hal_serial_detach(struct modem_softc *sc)
{
	int err = 0;

	err = close(sc->atif);
	if (err) {
		printf("Failed to close %s!\n", "/dev/ttyUSB2");
		return -1;
	}
	return 0;
}

static int modem_hal_open(struct radio_context *ctx, enum radio_type type)
{
	int err = 0;
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	if (debug)
		printf("modem HAL open\n");

	err = modem_hal_serial_attach(sc);
	if (err) {
		printf("failed to register modem\n");
		return -1;
	}
	return 0;
}

static int modem_hal_close(struct radio_context *ctx, enum radio_type type)
{
	int err = 0;
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	if (debug)
		printf("modem HAL close\n");

	err = modem_hal_serial_detach(sc);
	if (err) {
		printf("failed to register modem\n");
		return -1;
	}
	return 0;
}

/* See 27.007 annex B */
static const char *responses_error[] = {
		"ERROR",
		"+CMS ERROR:",
		"+CME ERROR:",
		"NO CARRIER", /* sometimes! */
		"NO ANSWER",
		"NO DIALTONE",
		nullptr
};

__attribute__((unused)) static int is_response_error(const char *line)
{
	size_t i;
	for (i = 0 ; responses_error[i] != nullptr ; i++) {
		if (strncmp(line, responses_error[i], strlen(responses_error[i])) == 0) {
			return 1;
		}
	}
	return 0;
}

/* See 27.007 annex B */
static const char *responses_success[] = {
		"OK",
		"CONNECT",
		nullptr
};
__attribute__((unused)) static int is_response_success(const char *line)
{
	size_t i;
	for (i = 0 ; responses_success[i] != nullptr ; i++) {
		if (strncmp(line, responses_success[i], strlen(responses_success[i])) == 0) {
			return 1;
		}
	}
	return 0;
}

int modem_hal_connect(struct radio_context *ctx, char *apn, char *pin)
{
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;
	int ret = 0;
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;

	radio_hal_prepare_cmd_buf(cmd_buf, sizeof(cmd_buf),
							  (const char*) "qmicli --device=/dev/%s --device-open-proxy --wds-start-network=\"ip-type=4,apn=%s\" --client-no-release-cid",
							  sc->modem, apn);
	ret = radio_hal_run_sys_cmd(cmd_buf, resp_buf, (int)len);
	if (ret) {
		printf("Failed to connect internet\n");
		return -1;
	}

	return ret;
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
	.radio_connect = modem_hal_connect,
};

__attribute__((unused)) int modem_hal_register_ops(struct radio_context *ctx)
{
	ctx->cmn.rd_func = &modem_hal_ops;

	return 0;
}

struct radio_context*  modem_hal_attach()
{
	struct radio_context *ctx = nullptr;
	struct modem_softc *sc = nullptr;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (!ctx) {
		printf("failed to allocate radio hal ctx\n");
		return nullptr;
	}

	sc = (struct modem_softc *)malloc(sizeof(struct modem_softc));
	if (!sc) {
			printf("failed to allocate modem softc ctx\n");
			free(ctx);
			return nullptr;
	}

	ctx->radio_private = (void*)sc;
	ctx->cmn.rd_func = &modem_hal_ops;
	printf("Modem HAL attach completed\n");

	return ctx;
}

int modem_hal_detach(struct radio_context *ctx)
{
	struct modem_softc *sc = (struct modem_softc *)ctx->radio_private;

	free(sc);
	free(ctx);

	printf("MODEM HAL detach completed\n");
	return 0;
}

