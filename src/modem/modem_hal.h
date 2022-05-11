#ifndef __MODEM_HAL_H__
#define __MODEM_HAL_H__

//Events
enum modem_SystemEvent {
	MODEM_NO_EVENT = -1,
	MODEM_STARTUP_EVENT,
	MODEM_DATA_CALL_EVENT,
	MODEM_NITZ_EVENT,
	MODEM_CELLULAR_TECH_EVENT,
	MODEM_OFF_EVENT,
	MODEM_REGISTRATION_EVENT,
	MODEM_LAST_EVENT
};

//States
enum modem_state {
	MODEM_UNKNOWN_STATE = -1,
	MODEM_INIT_STATE,
	MODEM_IF_DOWN_STATE,
	MODEM_IF_UP_STATE,
	MODEM_ASSOCIATED_STATE,
	MODEM_CONNECTED_STATE,
	MODEM_DISCONNECTED_STATE,
	MODEM_LAST_STATE  /* Don't remove */
};

//typedef of function pointer
typedef modem_state (*modemEventHandler)(struct radio_context *ctx, struct radio_hal_msg_buffer *msg);

//structure of state and event with event handler
typedef struct {
	modem_state StateMachine;
	modem_SystemEvent StateMachineEvent;
	modemEventHandler StateMachineEventHandler;
} modem_StateMachine;

struct modem_softc {
	int atif;
	char modem[20];
	char wwan[20];
	modem_state state;
};

typedef enum {
	SIM_ABSENT = 0,
	SIM_NOT_READY = 1,
	SIM_READY = 2,
	SIM_PIN = 3,
	SIM_PUK = 4,
	SIM_NETWORK_PERSONALIZATION = 5,
	RUIM_ABSENT = 6,
	RUIM_NOT_READY = 7,
	RUIM_READY = 8,
	RUIM_PIN = 9,
	RUIM_PUK = 10,
	RUIM_NETWORK_PERSONALIZATION = 11,
	ISIM_ABSENT = 12,
	ISIM_NOT_READY = 13,
	ISIM_READY = 14,
	ISIM_PIN = 15,
	ISIM_PUK = 16,
	ISIM_NETWORK_PERSONALIZATION = 17,
} SIM_Status;


struct radio_context* modem_hal_attach();
int modem_hal_detach(struct radio_context *ctx);

#endif
