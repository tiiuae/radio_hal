#include <cstdio>
#include <cstring>
#include <yaml.h>
#include "wifi_hal.h"
#include "radio_hal.h"
#include "radio_hal_yaml.h"

#if not defined RADIO_HAL_UNIT_TEST
static int debug = 1;

static int radio_hal_yaml_bt_config(struct bt_config* conf_struct, char *key, char *value) {
	if (debug)
		printf("radio_hal_bt_config: key: %s value: %s\n", key, value);
	return 0;
}

static int radio_hal_yaml_z_config(struct z_config* conf_struct, char *key, char *value) {
	if (debug)
		printf("radio_hal_z_config: key: %s value: %s\n", key, value);
	return 0;
}

static int radio_hal_yaml_wifi_config(struct wifi_config* conf_struct, char *key, char *value)
{
	/* TODO concurrency with single radio? */
	if (debug)
		printf("radio_hal_wifi_config: key: %s value: %s\n", key, value);

	if(!strcmp(key, "debug")) {
		conf_struct->debug = false;
		if (!strcmp(value, "True"))
			conf_struct->debug = true;
	} else if(!strcmp(key, "ssid")) {
		strcpy(conf_struct->ssid, value);
	} else if(!strcmp(key, "key")) {
		strcpy(conf_struct->key, value);
	} else if(!strcmp(key, "frequency")) {
		strcpy(conf_struct->freq, value);
	} else if(!strcmp(key, "api_version")) {
		// conf_struct->api_version
	} else if(!strcmp(key, "passphrase")) {
		//conf_struct->passphrase
	} else if(!strcmp(key, "enc")) {
		//conf_struct->enc
	} else if(!strcmp(key, "ap_mac")) {
		//conf_struct->ap_mac_addr
	} else if(!strcmp(key, "country")) {
		//conf_struct->country
	} else if(!strcmp(key, "ip")) {
		//conf_struct->ip
	} else if(!strcmp(key, "subnet")) {
		//conf_struct->subnet
	} else if(!strcmp(key, "tx_power")) {
		//conf_struct->tx_power
	} else if(!strcmp(key, "mode")) {
		//conf_struct->mode
	} else if(!strcmp(key, "type")) {
		//conf_struct->type
	} else
		printf("no data structure for key!\n");

	return 0;
}

int radio_hal_yaml_config(void *conf_struct, const char* yaml_file, radio_type radio)
{
	int ret = 0;
	yaml_token_t  token;   /* new variable */
	bool key = false;
	char temp_key[16] = {0};

	FILE *configuration = fopen(yaml_file, "r");
	if (!configuration) {
		printf("Failed to open %s!\n", yaml_file);
		return -1;
	}

	yaml_parser_t parser;

	/* Initialize parser */
	if(!yaml_parser_initialize(&parser)) {
		printf("Failed to open parser\n");
		fclose(configuration);
		return -1;
	}

	yaml_parser_set_input_file(&parser, configuration);

	do {
		yaml_parser_scan(&parser, &token);
		switch(token.type)
		{
			/*
			case YAML_STREAM_START_TOKEN: printf("STREAM START"); break;
			case YAML_STREAM_END_TOKEN:   printf("STREAM END");   break;
			case YAML_BLOCK_SEQUENCE_START_TOKEN: printf("<b>Start Block (Sequence)</b>"); break;
			case YAML_BLOCK_ENTRY_TOKEN:          printf("<b>Start Block (Entry)</b>");    break;
			case YAML_BLOCK_END_TOKEN:            printf("<b>End block</b>");              break;
			case YAML_BLOCK_MAPPING_START_TOKEN:  printf("[Block mapping]");            break;
			 */
			case YAML_KEY_TOKEN:
				key = true;
				break;
			case YAML_VALUE_TOKEN:
				key = false;
				break;
			case YAML_SCALAR_TOKEN:
				if (key) {
					memset(temp_key, 0, sizeof(temp_key));
					if (sizeof(temp_key)>strlen((const char *)token.data.scalar.value))
						strcpy(temp_key, (const char *)token.data.scalar.value);
				} else {
					switch(radio)
					{
						case RADIO_WIFI:
							ret = radio_hal_yaml_wifi_config((struct wifi_config*)conf_struct, temp_key, (char *)token.data.scalar.value);
							break;
						case RADIO_BT:
							ret = radio_hal_yaml_bt_config((struct bt_config*)conf_struct, temp_key, (char *)token.data.scalar.value);
							break;
						case RADIO_15_4:
							ret = radio_hal_yaml_z_config((struct z_config*)conf_struct, temp_key, (char *)token.data.scalar.value);
							break;
						default:
							printf("radio not supported for yaml parsing!\n");
							break;
					}
				}
				break;
			default:
				//printf("Got token of type %d\n", token.type);
				break;
		}
		if(token.type != YAML_STREAM_END_TOKEN)
			yaml_token_delete(&token);
	} while(token.type != YAML_STREAM_END_TOKEN);

	yaml_token_delete(&token);
	yaml_parser_delete(&parser);
	//printf("%s (%d events)\n", (error ? "FAILURE" : "SUCCESS"), count);

	fclose(configuration);

	return ret;
}
#endif