#include <stdio.h>
#include <getopt.h>
#include "radio_hal.h"
#include "wifi_hal.h"

int radio_hal_attach(struct radio_context *ctx, enum radio_type type)
{
	int err = 0;

	switch(type)
	{
		case RADIO_WIFI:
			err = wifi_hal_attach(ctx);
			break;
		case RADIO_BT:
			break;
		case RADIO_15_4:
			break;
	}

	return err;
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
	}

	return 0;
}

void show_radio_hal_help()
{
	printf("\n------------------------- Radio HAL uses --------------------------------\n");
	printf("./radio_hal_daemon -w <radio index> Attach wifi radio HAL \n");
	printf("./radio_hal_daemon -b <radio index> Attach BT radio HAL \n");
	printf("./radio_hal_daemon -b <radio index> Attach 15.4 radio HAL \n");
	printf("\n-------------------------- ----------------------------------------------\n");
}

int main(int argc, char *argv[])
{
	int status, c;
	const char	*short_opt = "w::b::z::h::";
	int long_opt_ptr;
	struct radio_context *ctx;
	struct option long_opt[] =
	{
		{"wifi", required_argument,0, 'w'},
		{"bt", required_argument,0, 'b'},
		{"Zigbee", required_argument,0, 'z'},
		{"help", optional_argument,0, 'h'}
	};

	printf("*  argc = %d argv = %s \n",argc,argv[0]);
	while((c = getopt_long(argc, argv, short_opt, long_opt, &long_opt_ptr)) != -1)
	{

		switch(c)
		{
			case 'w':
				status = radio_hal_attach(ctx, RADIO_WIFI);
				if (status)
					printf("failed to attach Wifi Radio HAL\n");
				break;
			case 'b':
				status = radio_hal_attach(ctx, RADIO_BT);
				if (status)
					printf("failed to attach BT Radio HAL\n");
				break;
			case 'z':
				status = radio_hal_attach(ctx, RADIO_15_4);
				if (status)
					printf("failed to attach 15.4 Radio HAL\n");
				break;
			case 'h':
				show_radio_hal_help();
				return(0);
		}
	}

	return 0;
}
