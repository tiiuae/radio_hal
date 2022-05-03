#include <sys/ipc.h>
#include <sys/msg.h>
#include "../../inc/radio_hal.h"
#include "debug.h"
#include <cerrno>

static key_t keys[RADIO_MAX] = {10001,
								10002,
								10003,
								10004};

int radio_hal_msg_queue_init(enum radio_type radio) {
	return msgget(keys[radio], 0666 | IPC_CREAT);
}

int radio_hal_msg_queue_destroy(enum radio_type radio, int msg_id) {
	return msgctl(msg_id, IPC_RMID, nullptr);
}

int radio_hal_msg_recv(struct radio_hal_msg_buffer *msg, int msg_id, enum radio_type radio) {
	long msgtyp = 0x80000000 | radio;
	int msgflg = 0;
	/* *
	 If msgtyp is 0, then the first message in the queue is read.
	 If msgtyp is greater than 0, then the first message in the queue of type
	 msgtyp is read, unless MSG_EXCEPT was specified in msgflg, in which case the
	 first message in the queue of type not equal to msgtyp will be read.
	 If msgtyp is less than 0, then the first message in the queue with the lowest
	 type less than or equal to the absolute value of msgtyp will be read.
	 * */

	if (msgrcv(msg_id, msg, sizeof(struct radio_hal_msg_buffer), msgtyp, msgflg) == -1) {
		if (errno != ENOMSG) {
			hal_err(HAL_DBG_COMMON, "msgrcv error %d\n", errno);
			return errno;
		}
	}
	return 0;
}

int radio_hal_msg_send(struct radio_hal_msg_buffer *msg, int msg_id, enum radio_type radio) {
	msg->mtype = 0x80000000 | radio;

	if (msgsnd(msg_id, &msg, sizeof(struct radio_hal_msg_buffer),
			   IPC_NOWAIT) == -1) {
		hal_err(HAL_DBG_COMMON, "msgsnd error\n");
	}
	return 0;
}
