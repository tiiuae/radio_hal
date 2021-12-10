#ifndef __MODEM_HAL_H__
#define __MODEM_HAL_H__

#include "radio_hal.h"

struct radio_context* modem_hal_attach();
int modem_hal_detach(struct radio_context *ctx);

#endif
