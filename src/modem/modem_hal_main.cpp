#include <cstring>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include "radio_hal.h"
#include "modem_hal.h"
#include "at/atchannel.h"
#include "at/at_tok.h"
#include "at/misc.h"
#include "at/ril.h"
#include "debug.h"
#include "../common/radio_hal_yaml.h"

#define CMD_BUFFER_SIZE sizeof(char) * 2048
#define MAC_ADDRESS_LENGTH sizeof(char) * 18 // aa:bb:cc:dd:ee:ff + NULL terminator
#define RESP_BUFFER_SIZE sizeof(char) * 2048
#define SOCKET_PATH_LENGTH sizeof(char) * 64

#define MODEM_RADIO_HAL_MAJOR_VERSION 0
#define MODEM_RADIO_HAL_MINOR_VERSION 1

void at_event_handler (const char *s, const char *sms_pdu) {
	/* todo event handler from URC at commands */
	hal_info(HAL_DBG_MODEM, "event: %s %s\n", s, sms_pdu);
}

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

static int modem_hal_check_modem() {
	int err = 0;
	char *manufacturer;
	char *model;
	ATResponse *at_response = nullptr;
 	const int mm_lenght = 10;
	manufacturer=(char*)malloc(mm_lenght);
	model=(char*)malloc(mm_lenght);

	/* todo model/manufacturer check AT+GMI / AT+GMM and continue if match*/
	err = at_send_command_singleline("AT+GMI", "", &at_response);
	if (err != 0)
		goto error;
	strncpy(manufacturer, at_response->p_intermediates->line, mm_lenght);

	err = at_send_command_singleline("AT+GMM", "", &at_response);
	if (err != 0)
		goto error;
	strncpy(model, at_response->p_intermediates->line, mm_lenght);


	hal_info(HAL_DBG_MODEM, "Manufacturer: %s\n", manufacturer);
	hal_info(HAL_DBG_MODEM, "Model: %s\n", model);

	if (strcmp(manufacturer, "Quectel") == 0 && strcmp(model, "EG25") == 0) {
		hal_info(HAL_DBG_MODEM, "Modem is supported and tested\n");
	}
	else {
		hal_warn(HAL_DBG_MODEM, "Modem is not supported and tested with Radio HAL\n");
		goto error;
	}

	at_response_free(at_response);
	free(manufacturer);
	free(model);
	return err;
error:
	at_response_free(at_response);
	free(manufacturer);
	free(model);
	return -1;
}

static int modem_hal_serial_attach(struct modem_softc *sc) {
	int ret = 0;
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int err;

	err = at_handshake();
	if (err) {
		hal_err(HAL_DBG_MODEM, "Failed to handshake %d\n", err);
		return -1;
	}

	/* model/manufacturer check AT+GMI / AT+GMM and continue if match*/
	ret = modem_hal_check_modem();
	if (ret) {
		hal_err(HAL_DBG_MODEM, "Failed in modem detection\n");
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

static void modem_hal_serial_detach(struct modem_softc *sc) {
	at_close();
}

static int modem_hal_open(struct radio_context *ctx, enum radio_type type, struct modem_config *config) {
	int err = 0;
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	hal_info(HAL_DBG_MODEM, "modem HAL open %s\n", config->at_serial);

	sc->atif = open(config->at_serial, O_RDWR | O_NOCTTY | O_SYNC);
	if (!sc->atif) {
		hal_err(HAL_DBG_MODEM, "Failed to open %s!\n", config->at_serial);
		return -1;
	}

	at_open(sc->atif, &at_event_handler);

	err = modem_hal_serial_attach(sc);
	if (err) {
		hal_err(HAL_DBG_MODEM, "failed to register modem\n");
		return -1;
	}
	return 0;
}

static int modem_hal_close(struct radio_context *ctx, enum radio_type type) {
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;

	hal_info(HAL_DBG_MODEM, "modem HAL close\n");

	at_send_command("AT+CFUN=4", nullptr);

	modem_hal_serial_detach(sc);
	sc->atif = 0;

	return 0;
}

static int modem_hal_check_pin_status(struct radio_context *ctx) {
	ATResponse *at_response = nullptr;
	int ret = 0;
	int err = 0;
	char *cpinLine;
	char *cpinResult;

	ret = at_send_command_singleline("AT+CPIN?", "+CPIN:", &at_response);

	if (ret != 0) {
		ret = SIM_NOT_READY;
		goto done;
	}

	switch (at_get_cme_error(at_response)) {
		case CME_SUCCESS:
			hal_info(HAL_DBG_MODEM, "SIM PIN OK\n");
			break;
		case CME_SIM_NOT_INSERTED:
			ret = SIM_ABSENT;
			hal_info(HAL_DBG_MODEM, "SIM Card Absent\n");
			goto done;
		default:
			ret = SIM_NOT_READY;
			hal_info(HAL_DBG_MODEM, "SIM Not Ready\n");
			goto done;
	}

	/* CPIN? has succeeded, now look at the result */
	cpinLine = at_response->p_intermediates->line;
	err = at_tok_start(&cpinLine);
	if (err < 0) {
		ret = SIM_NOT_READY;
		goto done;
	}
	err = at_tok_nextstr(&cpinLine, &cpinResult);
	if (err < 0) {
		ret = SIM_NOT_READY;
		goto done;
	}

	if (0 == strcmp(cpinResult, "SIM PIN")) {
		ret = SIM_PIN;
		goto done;
	} else if (0 == strcmp(cpinResult, "SIM PUK")) {
		ret = SIM_PUK;
		goto done;
	} else if (0 != strcmp(cpinResult, "READY"))  {
		/* we're treating unsupported lock types as "sim absent" */
		ret = SIM_ABSENT;
		goto done;
	}
	at_response_free(at_response);
	at_response = nullptr;
	cpinResult = nullptr;
	ret = SIM_READY;
done:
	at_response_free(at_response);
	at_response = nullptr;
	return ret;
}

static int modem_hal_get_numretries (int request) {
	ATResponse *p_response = nullptr;
	ATLine *p_cur = nullptr;
	int err;
	char *fac = nullptr;
	int pin1_remaining_times;
	int puk1_remaining_times;
	int pin2_remaining_times;
	int puk2_remaining_times;

	err = at_send_command_multiline("AT+QPINC?", "+QPINC", &p_response);

	if(err < 0  || p_response == nullptr || p_response->success == 0) goto error;

	for (p_cur = p_response->p_intermediates; p_cur != nullptr;p_cur = p_cur->p_next)
	{
		char *line = p_cur->line;

		at_tok_start(&line);

		err = at_tok_nextstr(&line,&fac);
		if(err < 0) goto error;

		if(!strncmp(fac,"SC",2))
		{
			err = at_tok_nextint(&line,&pin1_remaining_times);
			if(err < 0) goto error;
			err = at_tok_nextint(&line,&puk1_remaining_times);
			if(err < 0) goto error;
		}
		else if(!strncmp(fac,"P2",2))
		{
			err = at_tok_nextint(&line,&pin2_remaining_times);
			if(err < 0) goto error;
			err = at_tok_nextint(&line,&puk2_remaining_times);
			if(err < 0) goto error;
		}
		else
			goto error;
	}
	//printf("PIN1:%d\nPIN2:%d\nPUK1:%d\nPUK2:%d\n",pin1_remaining_times,pin2_remaining_times,puk1_remaining_times,puk2_remaining_times);

	free(p_response);
	free(p_cur);

	switch(request)
	{
		case RIL_REQUEST_SET_FACILITY_LOCK:
		case RIL_REQUEST_ENTER_SIM_PIN:
		case RIL_REQUEST_CHANGE_SIM_PIN:
			return pin1_remaining_times;
		case RIL_REQUEST_ENTER_SIM_PIN2:
		case RIL_REQUEST_CHANGE_SIM_PIN2:
			return pin2_remaining_times;
		case RIL_REQUEST_ENTER_SIM_PUK:
			return puk1_remaining_times;
		case RIL_REQUEST_ENTER_SIM_PUK2:
			return puk2_remaining_times;
		default:
			return -1;
	}

error:
	return -1;
}


static int enter_sim_pin(char *pin2) {
	ATResponse *atresponse = nullptr;
	int err;
	int ret = -1;  /* generic failure */
	//ATCmeError cme_error_code = -1;
	char *cpinLine = nullptr;
	char *cpinResult = nullptr;
	char *cmd = nullptr;
	int cmd_size = sizeof("AT+CPIN=\"12345678\"\n");

	cmd = (char*)malloc(cmd_size);
	prepare_cmd_buf(cmd, cmd_size , (const char*)"AT+CPIN=\"%s\"", pin2);
	err = at_send_command(cmd, &atresponse);

	if (err < 0)
		goto exit;

	if (atresponse->success == 0) {
		ret = -1;
		goto exit;
	}

	at_response_free(atresponse);
	atresponse = nullptr;

	/* CPIN set has succeeded, now look at the result. */
	err = at_send_command_singleline("AT+CPIN?", "+CPIN:", &atresponse);

	if (err < 0 || atresponse->success == 0)
		goto exit;

	cpinLine = atresponse->p_intermediates->line;

	if (at_tok_start(&cpinLine) < 0)
		goto exit;

	if (at_tok_nextstr(&cpinLine, &cpinResult) < 0)
		goto exit;

	if (0 == strcmp(cpinResult, "READY"))
		ret = 0;
	else if (0 == strcmp(cpinResult, "SIM PIN2"))
		ret = -2;
	else if (0 == strcmp(cpinResult, "SIM PUK2"))
		ret = -3;
	else
		ret = -4;

exit:
	free(cmd);
	at_response_free(atresponse);
	return ret;
}

static int modem_hal_get_registration_status()
{
	int err;
	char **responseStr = nullptr;
	ATResponse *p_response = nullptr;
	char *line;
	int urc;
	int registration_status;
	int wait_count = 0;

restart:

	err = at_send_command_singleline("AT+CGREG?", "+CGREG:", &p_response);
	if (err != 0)
		goto error;

	line = p_response->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto error;

	err = at_tok_nextint(&line,&urc);
	if (err < 0)
		goto error;


	/* Registration status values:
	 * 	0 Not registered. MT is not currently searching an operator to register to. The UE is in
			GMM state GMM-NULL or GMM-DEREGISTERED-INITIATED. The GPRS service is
			disabled, but the UE is allowed to attach for GPRS if requested by the user.
		1 Registered, home network. The UE is in GMM state GMM-REGISTERED or
			 GMM-ROUTING-AREA-UPDATING-INITIATED INITIATED on the home PLMN.
		2 Not registered, but MT is currently trying to attach or searching an operator to register
			to. UE is in GMM state GMM-DEREGISTERED or GMM-REGISTERED-INITIATED.
			The GPRS service is enabled, but an allowable PLMN is currently not available. The
			UE will start a GPRS attach as soon as an allowable PLMN is available.
		3 Registration denied. The UE is in GMM state GMM-NULL. The GPRS service is
			disabled, and the UE is not allowed to attach for GPRS if requested by the user.
		4 Unknown
		5 Registered, roaming
	 *
	 */

	err = at_tok_nextint(&line, &registration_status);
	if (err < 0)
		goto error;

	wait_count++;
	if (wait_count > 10) goto error;
	switch ((RIL_RegState)registration_status){
		case RIL_NOT_REG_AND_NOT_SEARCHING:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_NOT_REG_AND_NOT_SEARCHING\n");
			sleep(1);
			goto restart;
			break;
		case RIL_REG_HOME:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_REG_HOME\n");
			break;
		case RIL_NOT_REG_AND_SEARCHING:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_NOT_REG_AND_SEARCHING\n");
			sleep(1);
			goto restart;
			break;
		case RIL_REG_DENIED:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_REG_DENIED\n");
			break;
		case RIL_UNKNOWN:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_UNKNOWN\n");
			sleep(1);
			goto restart;
			break;
		case RIL_REG_ROAMING:
			hal_info(HAL_DBG_MODEM, "RIL_RegState RIL_REG_ROAMING\n");
			break;
		case RIL_NOT_REG_AND_EMERGENCY_AVAILABLE_AND_NOT_SEARCHING:
		case RIL_NOT_REG_AND_EMERGENCY_AVAILABLE_AND_SEARCHING:
		case RIL_REG_DENIED_AND_EMERGENCY_AVAILABLE:
		case RIL_UNKNOWN_AND_EMERGENCY_AVAILABLE:
			/* not handled as interested about data reg state */
			hal_info(HAL_DBG_MODEM, "RIL_RegState not handled %d\n", registration_status);
			break;
	}

	free(responseStr);
	at_response_free(p_response);
	return registration_status;

error:
	free(responseStr);
	at_response_free(p_response);
	return -1;
}

int modem_hal_connect(struct radio_context *ctx, char *apn, char *pin) {
	struct modem_softc *sc = (struct modem_softc *) ctx->radio_private;
	int ret = 0;
	char cmd_buf[CMD_BUFFER_SIZE] = {0};
	char resp_buf[RESP_BUFFER_SIZE] = {0};
	size_t len = sizeof(resp_buf) - 1;
	int pin1_retries = 0;

	ret = modem_hal_check_pin_status(ctx);

	switch(ret) {
		case SIM_ABSENT:
			hal_err(HAL_DBG_MODEM, "SIM_ABSENT Please insert SIM card\n");
			return -1;
		case SIM_NOT_READY:
			hal_err(HAL_DBG_MODEM, "SIM_NOT_READY ??\n");
			return -1;
		case SIM_READY:
			hal_info(HAL_DBG_MODEM, "SIM_READY for connection\n");
			break;
		case SIM_PIN:
			hal_info(HAL_DBG_MODEM, "SIM_PIN verification needed\n");
			pin1_retries = modem_hal_get_numretries(RIL_REQUEST_CHANGE_SIM_PIN);
			hal_info(HAL_DBG_MODEM, "SIM PIN retries = %d\n", pin1_retries);
			// don't use the last retry (if you don't have PUK code)
			if (pin1_retries > 1) {
				ret = enter_sim_pin(pin);
				if (ret) {
					hal_err(HAL_DBG_MODEM, "SIM PIN verify failed\n");
					return -1;
				}
				else
					hal_info(HAL_DBG_MODEM, "SIM PIN verified\n");
			}
			else {
				hal_err(HAL_DBG_MODEM, "Only 1 SIM PIN retry left, not used by radio_hal\n");
				return -1;
			}
			break;
		case SIM_PUK:
			hal_info(HAL_DBG_MODEM, "SIM_PUK verification needed\n");
			return -1;
	}

	/*  Network registration events */
	at_send_command("AT+CGREG=1", NULL);
	/* Turn modem online*/
	at_send_command("AT+CFUN=1", NULL);

	ret = modem_hal_get_registration_status();

	if (ret < 0) {
		hal_err(HAL_DBG_MODEM, "Network registration taking long time, state:%d\n", ret);
		return -1;
	}

	/* todo replace with libqmi API */
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

static int modem_hal_get_rssi(struct radio_context *ctx, int radio_index)
{
	//struct wifi_softc *sc = (struct wifi_softc *)ctx->radio_private;
	ATResponse *atresponse = nullptr;
	RIL_SignalStrength_v6 *signalStrength = nullptr;
	int err;
	char *line;
	int ber;
	int rssi;

	signalStrength = (RIL_SignalStrength_v6 *)malloc(sizeof(RIL_SignalStrength_v6));
	memset(signalStrength, 0, sizeof(RIL_SignalStrength_v6));

	signalStrength->LTE_SignalStrength.signalStrength = -1;
	signalStrength->LTE_SignalStrength.rsrp = -1;
	signalStrength->LTE_SignalStrength.rsrq = -1;
	signalStrength->LTE_SignalStrength.rssnr = -1;
	signalStrength->LTE_SignalStrength.cqi = -1;

	err = at_send_command_singleline("AT+CSQ", "+CSQ:", &atresponse);

	if (err != 0)
		goto cind;

	line = atresponse->p_intermediates->line;

	err = at_tok_start(&line);
	if (err < 0)
		goto cind;

	err = at_tok_nextint(&line,&rssi);
	if (err < 0)
		goto cind;
	signalStrength->GW_SignalStrength.signalStrength = rssi;

	err = at_tok_nextint(&line, &ber);
	if (err < 0)
		goto cind;
	signalStrength->GW_SignalStrength.bitErrorRate = ber;

	at_response_free(atresponse);
	atresponse = nullptr;
	/*
	 * If we get 99 as signal strength. Try AT+CIND to give
	 * some indication on what signal strength we got.
	 *
	 * Android calculates rssi and dBm values from this value, so the dBm
	 * value presented in android will be wrong, but this is an error on
	 * android's end.
	 */
	if (rssi == 99) {
cind:
		at_response_free(atresponse);
		atresponse = nullptr;

		err = at_send_command_singleline("AT+CIND?", "+CIND:", &atresponse);
		if (err != 0)
			goto error;

		line = atresponse->p_intermediates->line;

		err = at_tok_start(&line);
		if (err < 0)
			goto error;

		/* discard the first value */
		err = at_tok_nextint(&line,
							 &signalStrength->GW_SignalStrength.signalStrength);
		if (err < 0)
			goto error;

		err = at_tok_nextint(&line,
							 &signalStrength->GW_SignalStrength.signalStrength);
		if (err < 0)
			goto error;

		signalStrength->GW_SignalStrength.bitErrorRate = 99;

		/* Convert CIND value so Android understands it correctly */
		if (signalStrength->GW_SignalStrength.signalStrength > 0) {
			signalStrength->GW_SignalStrength.signalStrength *= 4;
			signalStrength->GW_SignalStrength.signalStrength--;
		}
	}

	at_response_free(atresponse);
	free(signalStrength);
	atresponse = nullptr;

	// Convert CSQ value to dBm
	return -113 + (rssi * 2);;

error:
	at_response_free(atresponse);
	free(signalStrength);
	atresponse = nullptr;
	return -1;
}

static struct radio_generic_func modem_hal_ops = {
		.open = nullptr,
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
		.radio_get_rssi = modem_hal_get_rssi,
		.radio_get_txrate = nullptr,
		.radio_get_rxrate = nullptr,
		.radio_get_scan_results = nullptr,
		.radio_connect_ap = nullptr,
		.radio_create_ap = nullptr,
		.radio_join_mesh = nullptr,
		.radio_connect = modem_hal_connect,
		.modem_open = modem_hal_open,
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

