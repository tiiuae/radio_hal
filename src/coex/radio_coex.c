#include <radio_coex.h>

void radio_set_coex_status(struct radio_coex_context *ctx, enum radio_coex_status status)
{
	BIT_SET(ctx->coex_status, status);
}

void radio_clear_coex_status(struct radio_coex_context *ctx, enum radio_coex_status status)
{
        BIT_CLEAR(ctx->coex_status, status);
}

int radio_get_coex_status(struct radio_coex_context *ctx)
{
        return ctx->coex_status;
}
