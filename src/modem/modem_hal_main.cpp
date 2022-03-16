#include <cstring>
#include <stdarg.h>
#include <fcntl.h>
#include "radio_hal.h"
#include "modem_hal.h"
#include "at/atchannel.h"
#include "at/at_tok.h"
#include "at/misc.h"
#include "at/ril.h"
#include "debug.h"

#define CMD_BUFFER_SIZE sizeof(char) * 2048
#define MAC_ADDRESS_LENGTH sizeof(char) * 18 // aa:bb:cc:dd:ee:ff + NULL terminator
#define RESP_BUFFER_SIZE sizeof(char) * 2048
#define SOCKET_PATH_LENGTH sizeof(char) * 64

#define MODEM_RADIO_HAL_MAJOR_VERSION 0
#define MODEM_RADIO_HAL_MINOR_VERSION 1

static ATUnsolHandler s_unsolHandler;
static ATResponse *at_response = NULL;


static int prepare_cmd_buf(char *buf, size_t len, const char *fmt, ...) {
	va_list args;

	if (buf != nullptr && fmt != nullptr) {
		memset(buf, 0, len - 1);
		va_start (args, fmt);
		vsnprintf(buf, len, fmt, args);
		va_end (args);
	} else
		return -1;

	return 0;
}

int modem_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size) {
	FILE *f;
	char *buf = resp_buf;
	int size = resp_size, resp_buf_bytes, readbytes;
	char *ret;

	if ((f = popen(cmd, "r")) == nullptr) {
		hal_err(HAL_DBG_MODEM, "popen %s error\n", cmd);
		return -1;
	}

	while (!feof(f)) {
		*buf = 0;
		if (size >= 128) {
			resp_buf_bytes = 128;
		} else {
			resp_buf_bytes = size - 1;
		}

		ret = fgets(buf, resp_buf_bytes, f);
		readbytes = (int) strlen(buf);
		if (!readbytes || ret == nullptr)
			break;

		size -= readbytes;
		buf += readbytes;

	}
	pclose(f);
	resp_buf[resp_size - 1] = 0;

	hal_debug(HAL_DBG_MODEM, "sys cmd:%s resp:%s\n", cmd, resp_buf);

	return 0;
}

static int modem_hal_get_hal_version(char *version) {
	snprintf(version, 32, "%d.%d", MODEM_RADIO_HAL_MAJOR_VERSION, MODEM_RADIO_HAL_MINOR_VERSION);
	return 0;
}

static int replace_line_change(char *buf, size_t size) {
	int len;

	len = strnlen(buf, size);
	if (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\0')) {
		buf[len - 1] = '\0';
	} else {
		hal_err(HAL_DBG_MODEM, "fail to find line change\n");
		return -1;
	}
	return 0;
}

static int modem_hal_find_modem(struct modem_softc *sc, char *resp_buf, int len) {
	int ret;

	ret = modem_hal_run_sys_cmd((char *) "ls /sys/class/usbmisc/ |grep cdc", resp_buf, len);
	if (ret) {
		hal_err(HAL_DBG_MODEM, "failed to get modem\n");
		return -1;
	}

	ret = replace_line_change(resp_buf, (size_t) len);
	if (ret < 0) {
		hal_err(HAL_DBG_MODEM, "fail with modem\n");
		return -1;
	}

	hal_debug(HAL_DBG_MODEM, "modem |%s|\n", resp_buf);

	return 0;
}

static int modem_hal_find_wwan(struct modem_softc *sc, char *resp_buf, int len) {
	int ret;
	char cmd[CMD_BUFFER_SIZE] = {0};

	prepare_cmd_buf(cmd, sizeof(cmd), (const char *) "qmicli --device=/dev/%s --get-wwan-iface", sc->modem);
	ret = modem_hal_run_sys_cmd(cmd, resp_buf, len);
	if (ret) {
		hal_err(HAL_DBG_MODEM, "failed to get wwan\n");
		return -1;
	}

	ret = replace_line_change(resp_buf, (size_t) len);
	if (ret < 0) {
		hal_err(HAL_DBG_MODEM, "fail with wwan\n");
		return -1;
	}

	hal_info(HAL_DBG_MODEM, "wwan |%s|\n", resp_buf);
	return 0;
}

static int modem_hal_serial_attach(struct modem_softc *sc) {
	int ret = 0;
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int err;

	sc->atif = open("/dev/ttyUSB2", O_RDWR | O_NOCTTY | O_SYNC);
	if (!sc->atif) {
		hal_err(HAL_DBG_MODEM, "Failed to open %s!\n", "/dev/ttyUSB2");
		return -1;
	}

	at_open(sc->atif, s_unsolHandler);

	err = at_handshake();
	if (err) {
		hal_err(HAL_DBG_MODEM, "Failed to handshake %d\n", err);
		return -1;
	}

	ret = modem_hal_find_modem(sc, resp_buf, len);
	if (ret) {
		hal_err(HAL_DBG_MODEM, "Failed modem_hal_get_modem()\n");
		return -1;
	}
	strcpy(sc->modem, resp_buf);

	ret = modem_hal_find_wwan(sc, resp_buf, len);
	if (ret) {
		hal_err(HAL_DBG_MODEM, "Failed modem_hal_get_wwan()\n");
		return -1;
	}
	strcpy(sc->wwan, resp_buf);

	prepare_cmd_buf(cmd_buf, sizeof(cmd_buf), (const char *) "echo Y > /sys/class/net/%s/qmi/raw_ip", sc->wwan);
	ret = modem_hal_run_sys_cmd(cmd_buf, resp_buf, (int) len);
	if (ret) {
		hal_err(HAL_DBG_MODEM, "Failed to turn on wwan raw_ip mode\n");
		return -1;
	}

	return 0;
}

static int modem_hal_serial_detach(struct modem_softc *sc) {

	at_close();

	return 0;
}

static int modem_hal_open(struct radio_context *ctx, enum radio_type type) {
	int err = 0;
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	hal_info(HAL_DBG_MODEM, "modem HAL open\n");

	err = modem_hal_serial_attach(sc);
	if (err) {
		hal_err(HAL_DBG_MODEM, "failed to register modem\n");
		return -1;
	}
	return 0;
}

static int modem_hal_close(struct radio_context *ctx, enum radio_type type) {
	int err = 0;
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	hal_info(HAL_DBG_MODEM, "modem HAL close\n");

	err = modem_hal_serial_detach(sc);
	if (err) {
		hal_err(HAL_DBG_MODEM, "failed to register modem\n");
		return -1;
	}
	return 0;
}

static int modem_hal_check_pin_status(struct radio_context *ctx) {
	at_response=at_response;
//	int ret = 0, err = 0;
//	char *cpinLine;
//	char *cpinResult;
//
//	ret = at_send_command_singleline("AT+CPIN?", "+CPIN:", &at_response);
//
//	if (ret != 0) {
//		ret = SIM_NOT_READY;
//		goto done;
//	}
//
//	switch (at_get_cme_error(at_response)) {
//		case CME_SUCCESS:
//			hal_info(HAL_DBG_MODEM, "SIM PIN OK\n");
//			break;
//		case CME_SIM_NOT_INSERTED:
//			ret = SIM_ABSENT;
//			hal_info(HAL_DBG_MODEM, "SIM Card Absent\n");
//			goto done;
//		default:
//			ret = SIM_NOT_READY;
//			hal_info(HAL_DBG_MODEM, "SIM Not Ready\n");
//			goto done;
//	}
//
//	/* CPIN? has succeeded, now look at the result */
//	cpinLine = at_response->p_intermediates->line;
//	err = at_tok_start(&cpinLine);
//	if (err < 0) {
//		ret = SIM_NOT_READY;
//		goto done;
//	}
//	err = at_tok_nextstr(&cpinLine, &cpinResult);
//	if (err < 0) {
//		ret = SIM_NOT_READY;
//		goto done;
//	}
//
//	if (0 == strcmp(cpinResult, "SIM PIN")) {
//		ret = SIM_PIN;
//		goto done;
//	} else if (0 == strcmp(cpinResult, "SIM PUK")) {
//		ret = SIM_PUK;
//		goto done;
//	} else if (0 != strcmp(cpinResult, "READY"))  {
//		/* we're treating unsupported lock types as "sim absent" */
//		ret = SIM_ABSENT;
//		goto done;
//	}
//	at_response_free(at_response);
//	at_response = NULL;
//	cpinResult = NULL;
//	ret = SIM_READY;
//done:
//	printf("ret = %d\n", ret);
//	at_response_free(at_response);
//	return ret;
	return 0;
}

int modem_hal_connect(struct radio_context *ctx, char *apn, char *pin) {
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;
	int ret = 0;
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;

	ret = modem_hal_check_pin_status(ctx);

	if (ret) {
		hal_err(HAL_DBG_MODEM, "pin status error\n");
		return -1;
	}

	prepare_cmd_buf(cmd_buf, sizeof(cmd_buf),
					(const char *) "qmicli --device=/dev/%s --device-open-proxy --wds-start-network=\"ip-type=4,apn=%s\" --client-no-release-cid",
					sc->modem, apn);
	ret = modem_hal_run_sys_cmd(cmd_buf, resp_buf, (int) len);

	if (ret) {
		hal_err(HAL_DBG_MODEM, "Failed to connect internet\n");
		return -1;
	}

	hal_info(HAL_DBG_MODEM, "Cellular connection started\n");

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

__attribute__((unused)) int modem_hal_register_ops(struct radio_context *ctx) {
	ctx->cmn.rd_func = &modem_hal_ops;

	return 0;
}

struct radio_context *modem_hal_attach() {
	struct radio_context *ctx = nullptr;
	struct modem_softc *sc = nullptr;

	ctx = (struct radio_context *) malloc(sizeof(struct radio_context));
	if (!ctx) {
		hal_err(HAL_DBG_MODEM, "failed to allocate radio hal ctx\n");
		return nullptr;
	}

	sc = (struct modem_softc *) malloc(sizeof(struct modem_softc));
	if (!sc) {
		hal_err(HAL_DBG_MODEM, "failed to allocate modem softc ctx\n");
		free(ctx);
		return nullptr;
	}

	ctx->radio_private = (void *) sc;
	ctx->cmn.rd_func = &modem_hal_ops;
	hal_info(HAL_DBG_MODEM, "Modem HAL attach completed\n");

	return ctx;
}

int modem_hal_detach(struct radio_context *ctx) {
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	free(sc);
	free(ctx);

	hal_info(HAL_DBG_MODEM, "MODEM HAL detach completed\n");
	return 0;
}

