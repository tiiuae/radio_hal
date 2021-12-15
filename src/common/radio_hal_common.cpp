#include <cstdio>
#include <cstring>
#include <cstdarg>
#include "radio_hal.h"
#include "wifi_hal.h"
#include "modem_hal.h"

static int debug;

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

int radio_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size)
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

int radio_hal_prepare_cmd_buf(char *buf, size_t len, const char *fmt, ...)
{
	if (buf != nullptr && fmt != nullptr) {
		va_list args;
		memset(buf, 0, len - 1);
		va_start (args, fmt);
		vsnprintf(buf, len, fmt, args);
		va_end (args);
	} else {
		return -1;
	}
	return 0;
}