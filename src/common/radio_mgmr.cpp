#include <getopt.h>
#include <cstdlib>
#include <unistd.h>
#include <pthread.h>
#include "debug.h"
#include "radio_hal.h"
#include "wifi_hal.h"
#include "modem_hal.h"
#include "radio_hal_yaml.h"


#ifndef RADIO_HAL_UNIT_TEST

#define EVENT_THREAD_AMOUNT  5
static pthread_t s_event_reader[EVENT_THREAD_AMOUNT];

static void *wifi_event_loop(void *arg) {
	struct radio_context *ctx = (struct radio_context *)arg;
	struct radio_generic_func *radio_ops = ctx->cmn.rd_func;

	hal_info(HAL_DBG_WIFI, "wifi_event_loop started\n");

	// starts event loop handler
	radio_ops->radio_event_loop(ctx);

	radio_ops->close(ctx, RADIO_WIFI);
	return nullptr;
}

static void *modem_event_loop(void *arg) {
	struct radio_context *ctx = (struct radio_context *)arg;
	struct radio_generic_func *radio_ops = ctx->cmn.rd_func;

	hal_info(HAL_DBG_MODEM, "modem_event_loop started\n");

	// starts event loop handler
	radio_ops->radio_event_loop(ctx);

	radio_ops->close(ctx, RADIO_MODEM);
	return nullptr;
}

static void show_radio_hal_help() {
	printf("\n------------------------- Radio HAL uses --------------------------------\n");
	printf("./radio_manager -w <yaml configuration file wifi>\n");
	printf("./radio_manager -b <yaml configuration file bluetooth>\n");
	printf("./radio_manager -z <yaml configuration file 15.4 radio>\n");
	printf("./radio_manager -m <yaml configuration file modem>\n");
	printf("\n-------------------------- ----------------------------------------------\n");
}

int main(int argc, char *argv[]) {
	int c, count = 0, ret = 0;
	const char *short_opt = "w:b:z:m:h";
	int long_opt_ptr;
	//wifi
	struct radio_context *w_ctx = nullptr;
	struct radio_generic_func *w_radio_ops;
	//modem
	struct radio_context *m_ctx = nullptr;
	struct radio_generic_func *m_radio_ops;
	//other
	struct radio_context *z_ctx = nullptr;
	struct radio_context *b_ctx = nullptr;
	//int index = atoi(argv[2]);

	struct option long_opt[] =
			{
					{"wifi",   required_argument, nullptr, 'w'},
					{"bt",     required_argument, nullptr, 'b'},
					{"Zigbee", required_argument, nullptr, 'z'},
					{"modem",  required_argument, nullptr, 'm'},
					{"help",   no_argument, nullptr, 'h'}
			};

	hal_info(HAL_DBG_COMMON, "*  argc = %d argv = %s %s\n", argc, argv[0], argv[2]);
	while ((c = getopt_long(argc, argv, short_opt, long_opt, &long_opt_ptr)) != -1) {
		switch (c) {
			case 'w':
				w_ctx = radio_hal_attach(RADIO_WIFI);
				if (!w_ctx || !w_ctx->cmn.rd_func) {
					hal_err(HAL_DBG_WIFI, "failed to attach Wifi Radio HAL\n");
					return -1;
				} else
					w_radio_ops = w_ctx->cmn.rd_func;

				for (int i = 0; i<WIFI_RADIO_MAX; i++)
					w_ctx->config[i] = malloc(sizeof(wifi_config));

				if (optarg)
					ret = radio_hal_yaml_config(w_ctx->config, (char *) optarg, RADIO_WIFI);
				if (ret < 0)
					return -1;

				w_radio_ops->open(w_ctx, RADIO_WIFI);
				if (count < EVENT_THREAD_AMOUNT) {
					ret = pthread_create(&s_event_reader[count++], nullptr, &wifi_event_loop, (void *) w_ctx);
					if (ret < 0) {
						hal_err(HAL_DBG_WIFI, "pthread_create\n");
						return -1;
					}
				} else
					hal_err(HAL_DBG_WIFI, "EVENT_THREAD_AMOUNT reached!\n");
				break;
			case 'b':
				b_ctx = radio_hal_attach(RADIO_BT);
				if (!b_ctx)
					hal_err(HAL_DBG_BT, "failed to attach BT Radio HAL\n");
				b_ctx->config[0] = malloc(sizeof(bt_config));
				ret = radio_hal_yaml_config(b_ctx->config, (char *)optarg, RADIO_BT);
				break;
			case 'z':
				z_ctx = radio_hal_attach(RADIO_15_4);
				if (!z_ctx)
					hal_err(HAL_DBG_BT, "failed to attach 15.4 Radio HAL\n");
				z_ctx->config[0] = malloc(sizeof(z_config));
				ret = radio_hal_yaml_config(z_ctx->config, (char *)optarg, RADIO_15_4);
				break;
			case 'm':
				m_ctx = radio_hal_attach(RADIO_MODEM);
				if (!m_ctx || !m_ctx->cmn.rd_func) {
					hal_err(HAL_DBG_MODEM, "failed to attach Wifi Radio HAL\n");
					return -1;
				} else
					m_radio_ops = m_ctx->cmn.rd_func;

				m_ctx->config[0] = malloc(sizeof(modem_config));
				if (optarg)
					ret = radio_hal_yaml_config(m_ctx->config, (char *) optarg,  RADIO_MODEM);
				if (ret < 0)
					return -1;

				ret = m_radio_ops->open(m_ctx, RADIO_MODEM);
				if (ret < 0)
					return -1;

				if (count < EVENT_THREAD_AMOUNT) {
					ret = pthread_create(&s_event_reader[count++], nullptr, &modem_event_loop, (void *) m_ctx);
					if (ret < 0) {
						hal_err(HAL_DBG_WIFI, "pthread_create\n");
						return -1;
					}
				} else
					hal_err(HAL_DBG_WIFI, "EVENT_THREAD_AMOUNT reached!\n");
				break;
			case 'h':
				show_radio_hal_help();
				return (0);
			default:
				hal_warn(HAL_DBG_COMMON, "Argument not supported: %c\n", c);
				return (0);
		}
	}

	// Keep child threads running
	for (int i = 0; i < count; i++) {
		pthread_join(s_event_reader[i], NULL);
	}
	return 0;
}

#endif