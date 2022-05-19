#include <cstring>
#include <yaml.h>
#include <cerrno>
#include "radio_hal.h"
#include "radio_hal_yaml.h"
#include "debug.h"

#if not defined RADIO_HAL_UNIT_TEST
static int debug = 1;

#define member_size(type, member) sizeof(((type *)0)->member)

static int radio_hal_yaml_bt_config(struct bt_config **conf_struct, char *key, char *value, int index) {
	if (debug)
		hal_debug(HAL_DBG_BT, "radio_hal_bt_config: key: %s value: %s\n", key, value);
	return 0;
}

static int radio_hal_yaml_z_config(struct z_config **conf_struct, char *key, char *value, int index) {
	if (debug)
		hal_debug(HAL_DBG_BT, "radio_hal_z_config: key: %s value: %s\n", key, value);
	return 0;
}

static int radio_hal_yaml_wifi_config(struct wifi_config **conf, char *key, char *value, int index)
{
	char *stopped;

	struct wifi_config *conf_struct = conf[index];

	/* TODO concurrency with single radio? */
	if (debug)
		hal_debug(HAL_DBG_WIFI, "radio_hal_wifi_config: key: %s value: %s\n", key, value);

	if(!strncmp(key, "debug", strlen("debug"))) {
		conf_struct->debug = false;
		if (!strncmp(value, "True",strlen("True")))
			conf_struct->debug = true;
	} else if(!strncmp(key, "ssid", strlen("ssid"))) {
		if (strlen(value) - 1 < member_size(wifi_config, ssid))
			strncpy(conf_struct->ssid, value, member_size(wifi_config, ssid));
	} else if(!strncmp(key, "key", strlen("key"))) {
		if (strlen(value) - 1 < member_size(wifi_config, key))
			strncpy(conf_struct->key, value, member_size(wifi_config, key));
	} else if(!strncmp(key, "frequency", strlen("frequency"))) {
		if (strlen(value) - 1 < member_size(wifi_config, freq))
			strncpy(conf_struct->freq, value, member_size(wifi_config, freq));
	} else if(!strncmp(key, "api_version", strlen("api_version"))) {
		errno = 0;
		conf_struct->api_version = (int)strtol(value, &stopped, 10);
		if (errno) {
			hal_err(HAL_DBG_WIFI, "key: %s value: %s not valid\n", key, value);
		}
	} else if(!strncmp(key, "passphrase", strlen("passphrase"))) {
		if (strlen(value) - 1 < member_size(wifi_config, passphrase))
			strncpy(conf_struct->passphrase, value, member_size(wifi_config, passphrase));
	} else if(!strncmp(key, "enc", strlen("enc"))) {
		if (strlen(value) - 1 < member_size(wifi_config, enc))
			strncpy(conf_struct->enc, value, member_size(wifi_config, enc));
	} else if(!strncmp(key, "ap_mac", strlen("ap_mac"))) {
		if (strlen(value) - 1 < member_size(wifi_config, ap_mac_addr))
			strncpy(conf_struct->ap_mac_addr, value, member_size(wifi_config, ap_mac_addr));
	} else if(!strncmp(key, "country", strlen("country"))) {
		if (strlen(value) - 1 < member_size(wifi_config, country))
			strncpy(conf_struct->country, value, member_size(wifi_config, country));
	} else if(!strncmp(key, "bw", strlen("bw"))) {
		if (strlen(value) - 1 < member_size(wifi_config, bw))
			strncpy(conf_struct->bw, value, member_size(wifi_config, bw));
	} else if(!strncmp(key, "preamble", strlen("preamble"))) {
		if (strlen(value) - 1 < member_size(wifi_config, preamble))
			strncpy(conf_struct->preamble, value, member_size(wifi_config, preamble));
	} else if(!strncmp(key, "distance", strlen("distance"))) {
		errno = 0;
		conf_struct->distance= (int)strtol(value, &stopped, 10);
		if (errno) {
			hal_err(HAL_DBG_WIFI, "key: %s value: %s not valid\n", key, value);
		}
	} else if(!strncmp(key, "tx_power", strlen("tx_power"))) {
		errno = 0;
		conf_struct->tx_power = (int)strtol(value, &stopped, 10);
		if (errno) {
			hal_err(HAL_DBG_WIFI, "key: %s value: %s not valid\n", key, value);
		}
	} else if(!strncmp(key, "mode", strlen("mode"))) {
		if (strlen(value) - 1 < member_size(wifi_config, mode))
			strncpy(conf_struct->mode, value, member_size(wifi_config, mode));
	} else if(!strncmp(key, "type", strlen("type"))) {
		if (strlen(value) - 1 < member_size(wifi_config, type))
			strncpy(conf_struct->type, value, member_size(wifi_config, type));
	} else
		hal_warn(HAL_DBG_WIFI, "no data structure for key!\n");

	return 0;
}

static int radio_hal_yaml_modem_config(struct modem_config **conf, char *key, char *value, int index) {

	struct modem_config *conf_struct = conf[index];

	if (debug)
		hal_debug(HAL_DBG_MODEM, "radio_hal_modem_config: key: %s value: %s\n", key, value);

	if(!strncmp(key, "apn", strlen("apn"))) {
		if (strlen(value) - 1 < member_size(modem_config, apn))
			strncpy(conf_struct->apn, value, member_size(modem_config, apn));
	} else if(!strncmp(key, "pin", strlen("pin"))) {
		if (strlen(value) - 1 < member_size(modem_config, pin))
			strncpy(conf_struct->pin, value, member_size(modem_config, pin));
	} else if(!strncmp(key, "at_serial", strlen("at_serial"))) {
		if (strlen(value) - 1 < member_size(modem_config, at_serial))
			strncpy(conf_struct->at_serial, value, member_size(modem_config, at_serial));
	} else
		hal_warn(HAL_DBG_MODEM, "no data structure for key!\n");
	return 0;
}

int radio_hal_yaml_config(void *conf_struct, char* config_files, radio_type radio)
{
	int ret = 0;
	yaml_token_t  token;   /* new variable */
	bool key = false;
	char temp_key[16] = {0};
	char *file_name;
	int index = 0;

	if (!config_files[0])
		return -1;

	file_name = strtok(config_files, " ");

	while( file_name != nullptr ) {

		printf(" %s\n", file_name); //printing the token

		FILE *configuration = fopen(file_name, "r");
		if (!configuration) {
			hal_err(HAL_DBG_COMMON, "Failed to open %s!\n", file_name);
			return -1;
		}

		yaml_parser_t parser;

		/* Initialize parser */
		if (!yaml_parser_initialize(&parser)) {
			hal_err(HAL_DBG_COMMON, "Failed to open parser\n");
			fclose(configuration);
			return -1;
		}

		yaml_parser_set_input_file(&parser, configuration);

		do {
			yaml_parser_scan(&parser, &token);
			switch (token.type) {
				case YAML_KEY_TOKEN:
					key = true;
					break;
				case YAML_VALUE_TOKEN:
					key = false;
					break;
				case YAML_SCALAR_TOKEN:
					if (key) {
						memset(temp_key, 0, sizeof(temp_key));
						if (sizeof(temp_key) > strlen((const char *) token.data.scalar.value))
							strcpy(temp_key, (const char *) token.data.scalar.value);
					} else {
						switch (radio) {
							case RADIO_WIFI:
								ret = radio_hal_yaml_wifi_config((struct wifi_config **) conf_struct, temp_key,
																 (char *) token.data.scalar.value, index);
								break;
							case RADIO_BT:
								ret = radio_hal_yaml_bt_config((struct bt_config **) conf_struct, temp_key,
															   (char *) token.data.scalar.value, index);
								break;
							case RADIO_15_4:
								ret = radio_hal_yaml_z_config((struct z_config **) conf_struct, temp_key,
															  (char *) token.data.scalar.value, index);
								break;
							case RADIO_MODEM:
								ret = radio_hal_yaml_modem_config((struct modem_config **) conf_struct, temp_key,
																  (char *) token.data.scalar.value, index);
								break;
							default:
								hal_warn(HAL_DBG_COMMON, "radio not supported for yaml parsing!\n");
								break;
						}
					}
					break;
				default:
					break;
			}
			if (token.type != YAML_STREAM_END_TOKEN)
				yaml_token_delete(&token);
		} while (token.type != YAML_STREAM_END_TOKEN);

		yaml_token_delete(&token);
		yaml_parser_delete(&parser);
		fclose(configuration);

		file_name = strtok(NULL, " ");
		index++;
	}
	return ret;
}
#endif