#ifndef RADIO_HAL_COMMON_H
#define RADIO_HAL_COMMON_H

#define CMD_BUFFER_SIZE sizeof(char) * 2048
#define RESP_BUFFER_SIZE sizeof(char) * 2048

int radio_hal_run_sys_cmd(char *cmd, char *resp_buf, int resp_size);
int radio_hal_prepare_cmd_buf(char *buf, size_t len, const char *fmt, ...);

#endif //RADIO_HAL_COMMON_H
