#ifndef RADIO_HAL_MODEM_EVENTS_H
#define RADIO_HAL_MODEM_EVENTS_H

//Events
typedef enum {
	no_event = -1,
	startup_event,
	last_Event
} modem_SystemEvent;

//States
typedef enum {
	no_state = -1,
	init_state,
	initialised_state,
	nitz_event,
	registration_event,
	data_call_event,
	cellular_tech_event,
	modem_off_event,
	last_State
} modem_SystemState;

//typedef of function pointer
typedef modem_SystemState (*fpEventHandler)(struct radio_context *ctx);

//structure of state and event with event handler
typedef struct {
	modem_SystemState StateMachine;
	modem_SystemEvent StateMachineEvent;
	fpEventHandler fpStateMachineEventHandler;
} modem_StateMachine;
#endif //RADIO_HAL_MODEM_EVENTS_H
