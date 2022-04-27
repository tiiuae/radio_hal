#ifndef RADIO_HAL_WIFI_EVENTS_H
#define RADIO_HAL_WIFI_EVENTS_H

//Events
typedef enum {
    no_event = -1,
    startup_event,
    ap_enabled_event,
    mesh_group_started_event,
    disconnected_event,
    connected_event,
    terminate_event,
    last_Event
} wifi_eSystemEvent;

//States
typedef enum {
    no_state = -1,
    init_state,
	initialised_state,
    ap_enabled_state,
    mesh_group_started_state,
    disconnected_state,
    connected_state,
    terminate_state,
    last_State
} wifi_eSystemState;

//typedef of function pointer
typedef wifi_eSystemState (*fpEventHandler)(struct radio_context *ctx);

//structure of state and event with event handler
typedef struct {
    wifi_eSystemState eStateMachine;
    wifi_eSystemEvent eStateMachineEvent;
    fpEventHandler fpStateMachineEventHandler;
} wifi_StateMachine;

#endif //RADIO_HAL_WIFI_EVENTS_H
