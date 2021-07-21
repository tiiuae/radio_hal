#include "radio_hal.h"
#include "wifi_hal.h"

int wifi_hal_attach(struct radio_context *ctx)
{
	int err = 0;

	ctx = (struct radio_context *)malloc(sizeof(struct radio_context));
	if (ctx == NULL) {
		printf("failed to allocate radio hal ctx");
		return -ENOMEM;
	}

	return err;
}

int wifi_hal_dettach(struct radio_context *ctx)
{
	int err = 0;

	if (ctx)
		free(ctx);

	return err;
}
