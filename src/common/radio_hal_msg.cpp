#include <sys/ipc.h>
#include <sys/msg.h>
#include "../../inc/radio_hal.h"

static char ftok_paths[RADIO_MAX][20] = {
		"/tmp/RADIO_WIFI",
		"/tmp/RADIO_BT",
		"/tmp/RADIO_15_4",
		"progfile"};

key_t radio_hal_msg_queue_init(enum radio_type radio, int *msg_id, int proj_id) {

    key_t key;
	// ftok to generate unique key
	key = ftok(ftok_paths[radio], proj_id);

	// msgget creates a message queue and returns identifier
	*msg_id = msgget(key, 0666 | IPC_CREAT);

	return key;
}

int radio_hal_msg_queue_destroy(enum radio_type radio, int msg_id) {

	int ret = 0;

	ret = msgctl(msg_id, IPC_RMID, nullptr);

	return ret;
}

int radio_hal_msg_recv(struct radio_hal_msg_buffer *msg, int msg_id, enum radio_type radio) {
	int ret = 0;
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
	ret = msgrcv(msg_id, &msg, sizeof(msg), msgtyp, msgflg);

	return ret;
}

int radio_hal_msg_send(struct radio_hal_msg_buffer *msg, int msg_id, enum radio_type radio, bool high_prio) {
	int ret = 0;
	int msgflg = (int)high_prio;

	msg->mtype = 0x80000000 | radio;
	ret = msgsnd(msg_id, &msg, sizeof(msg), msgflg);

	return ret;
}
