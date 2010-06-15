/*
* Copyright (C) 2009 Mamadou Diop.
*
* Contact: Mamadou Diop <diopmamadou(at)doubango.org>
*	
* This file is part of Open Source Doubango Framework.
*
* DOUBANGO is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as publishd by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*	
* DOUBANGO is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*	
* You should have received a copy of the GNU General Public License
* along with DOUBANGO.
*
*/

/**@file tsip_dialog_invite.c
 * @brief SIP dialog INVITE as per RFC 3261.
 * The SOA machine is designed as per RFC 3264 and draft-ietf-sipping-sip-offeranswer-12.
 * MMTel services implementation follow 3GPP TS 24.173.
 *
 * @author Mamadou Diop <diopmamadou(at)doubango.org>
 *
 * @date Created: Sat Nov 8 16:54:58 2009 mdiop
 */
#include "tinysip/dialogs/tsip_dialog_invite.h"

#include "tinysip/dialogs/tsip_dialog_invite.common.h"

#include "tinysip/transports/tsip_transport_layer.h"

#include "tinysip/headers/tsip_header_Min_SE.h"
#include "tinysip/headers/tsip_header_RAck.h"
#include "tinysip/headers/tsip_header_RSeq.h"
#include "tinysip/headers/tsip_header_Session_Expires.h"
#include "tinysip/headers/tsip_header_Supported.h"

#include "tinysdp/parsers/tsdp_parser_message.h"

#include "tsk_debug.h"

// http://cdnet.stpi.org.tw/techroom/market/_pdf/2009/eetelecomm_09_009_OneVoiceProfile.pdf
// 3GPP TS 26.114 (MMTel): Media handling and interaction
// 3GPP TS 24.173 (MMTel): Supplementary Services
//
/* ======================== MMTel Supplementary Services ======================== 
3GPP TS 24.607 : Originating Identification Presentation
3GPP TS 24.608 : Terminating Identification Presentation
3GPP TS 24.607 : Originating Identification Restriction
3GPP TS 24.608 : Terminating Identification Restriction 

3GPP TS 24.604 : Communication Diversion Unconditional
3GPP TS 24.604 : Communication Diversion on not Logged
3GPP TS 24.604 : Communication Diversion on Busy 
3GPP TS 24.604 : Communication Diversion on not Reachable
3GPP TS 24.604 : Communication Diversion on No Reply
3GPP TS 24.611 : Barring of All Incoming Calls
3GPP TS 24.611 : Barring of All Outgoing Calls
3GPP TS 24.611 : Barring of Outgoing International Calls
3GPP TS 24.611 : Barring of Incoming Calls - When Roaming
3GPP TS 24.610 : Communication Hold 
3GPP TS 24.606 : Message Waiting Indication
3GPP TS 24.615 : Communication Waiting
3GPP TS 24.605 : Ad-Hoc Multi Party Conference
*/

/* ======================== internal functions ======================== */
int send_INVITEorUPDATE(tsip_dialog_invite_t *self, tsk_bool_t is_INVITE);
int send_PRACK(tsip_dialog_invite_t *self, const tsip_response_t* r1xx);
int send_ACK(tsip_dialog_invite_t *self, const tsip_response_t* r2xxINVITE);
int send_RESPONSE(tsip_dialog_invite_t *self, const tsip_request_t* request, short code, const char* phrase);
int send_BYE(tsip_dialog_invite_t *self);
int send_CANCEL(tsip_dialog_invite_t *self);
int tsip_dialog_invite_OnTerminated(tsip_dialog_invite_t *self);

/* ======================== external functions ======================== */
extern int tsip_dialog_invite_stimers_cancel(tsip_dialog_invite_t* self);
extern int tsip_dialog_invite_stimers_schedule(tsip_dialog_invite_t* self, uint64_t timeout);
extern int tsip_dialog_invite_stimers_handle(tsip_dialog_invite_t* self, const tsip_message_t* message);

/* ======================== transitions ======================== */
extern int c0000_Started_2_Outgoing_X_oINVITE(va_list *app);
extern int c0001_Outgoing_2_Connected_X_i2xxINVITE(va_list *app);
extern int c0002_Outgoing_2_Terminated_X_i300_to_i699INVITE(va_list *app);
extern int c0003_Outgoing_2_Terminated_X_oCANCEL(va_list *app);

extern int s0000_Started_2_Incoming_X_iINVITE(va_list *app);
extern int s0000_Incoming_2_Connected_X_o2xx(va_list *app); // 2xx INVITE
extern int s0000_Incoming_2_Terminated_X_oCANCEL(va_list *app);

static int x0000_Connected_2_Connected_X_iINVITEorUPDATE(va_list *app);

static int x0000_Any_2_Any_X_iOPTIONS(va_list *app);
static int x0000_Any_2_Any_X_i2xxINVITEorUPDATE(va_list *app);
static int x0000_Any_2_Trying_X_oBYE(va_list *app); /* If not Connected => Cancel will be called instead. See tsip_dialog_hangup() */
static int x0000_Any_2_Terminated_X_iBYE(va_list *app);
static int x0000_Any_2_Trying_X_shutdown(va_list *app);

static int x0000_Any_2_Trying_X_oINVITE(va_list *app);
static int x0000_Any_2_Trying_X_oUPDATE(va_list *app);
static int x0000_Any_2_Any_X_iINVITE(va_list *app);
static int x0000_Any_2_Any_X_iUPDATE(va_list *app);
static int x0000_Any_2_Any_X_i1xx(va_list *app);
static int x0000_Any_2_Any_X_i2xx(va_list *app);
static int x0000_Any_2_Any_X_i401_i407_i421_i494(va_list *app);
static int x0000_Any_2_Any_X_iPRACK(va_list *app);
static int x0000_Any_2_Any_X_iACK(va_list *app);
static int x9998_Any_2_Any_X_transportError(va_list *app);
static int x9999_Any_2_Any_X_Error(va_list *app);


/* ======================== conds ======================== */
static tsk_bool_t _fsm_cond_is_resp2INVITE(tsip_dialog_invite_t* self, tsip_message_t* message)
{
	return TSIP_RESPONSE_IS_TO_INVITE(message);
}
static tsk_bool_t _fsm_cond_is_resp2UPDATE(tsip_dialog_invite_t* self, tsip_message_t* message)
{
	return TSIP_RESPONSE_IS_TO_UPDATE(message);
}
static tsk_bool_t _fsm_cond_is_resp2BYE(tsip_dialog_invite_t* self, tsip_message_t* message)
{
	return TSIP_RESPONSE_IS_TO_BYE(message);
}
static tsk_bool_t _fsm_cond_is_resp2PRACK(tsip_dialog_invite_t* self, tsip_message_t* message)
{
	return TSIP_RESPONSE_IS_TO_PRACK(message);
}

/* ======================== actions ======================== */
/* #include "tinysip/dialogs/tsip_dialog_invite.common.h" */

/* ======================== states ======================== */
/* #include "tinysip/dialogs/tsip_dialog_invite.common.h" */



/* 3GPP TS 24.610: Communication Hold  */
extern int tsip_dialog_invite_hold_init(tsip_dialog_invite_t *self);
/* RFC 4028: Session Timers */
extern int tsip_dialog_invite_stimers_init(tsip_dialog_invite_t *self);

int tsip_dialog_invite_event_callback(const tsip_dialog_invite_t *self, tsip_dialog_event_type_t type, const tsip_message_t *msg)
{
	int ret = -1;

	switch(type)
	{
	case tsip_dialog_i_msg:
		{
			if(msg){
				if(TSIP_MESSAGE_IS_RESPONSE(msg)){ /* Response */
					if(TSIP_RESPONSE_IS_1XX(msg)){ // 100-199
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_i1xx, msg, tsk_null);
					}
					else if(TSIP_RESPONSE_IS_2XX(msg)){ // 200-299
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_i2xx, msg, tsk_null);
					}
					else if(TSIP_RESPONSE_CODE(msg) == 401 || TSIP_RESPONSE_CODE(msg) == 407 || TSIP_RESPONSE_CODE(msg) == 421 || TSIP_RESPONSE_CODE(msg) == 494){ // 401,407,421,494
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_i401_i407_i421_i494, msg, tsk_null);
					}
					else if(TSIP_RESPONSE_IS_3456(msg)){ // 300-699
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_i300_to_i699, msg, tsk_null);
					}
					else; // Ignore
				}
				else{ /* Request */
					if(TSIP_REQUEST_IS_INVITE(msg)){ // INVITE
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_iINVITE, msg, tsk_null);
					}
					else if(TSIP_REQUEST_IS_UPDATE(msg)){ // UPDATE
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_iUPDATE, msg, tsk_null);
					}
					else if(TSIP_REQUEST_IS_PRACK(msg)){ // PRACK
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_iPRACK, msg, tsk_null);
					}
					else if(TSIP_REQUEST_IS_ACK(msg)){ // ACK
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_iACK, msg, tsk_null);
					}
					else if(TSIP_REQUEST_IS_OPTIONS(msg)){ // OPTIONS
						ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_iOPTIONS, msg, tsk_null);
					}
				}
			}
			break;
		}

	case tsip_dialog_canceled:
		{
			ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_oCANCEL, msg, tsk_null);
			break;
		}

	case tsip_dialog_terminated:
	case tsip_dialog_timedout:
	case tsip_dialog_error:
	case tsip_dialog_transport_error:
		{
			ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_transporterror, msg, tsk_null);
			break;
		}
	}

	return ret;
}

/**Timer manager callback.
 *
 * @param self	The owner of the signaled timer. 
 * @param	timer_id		The identifier of the signaled timer.
 *
 * @return	Zero if succeed and non-zero error code otherwise.  
**/
int tsip_dialog_invite_timer_callback(const tsip_dialog_invite_t* self, tsk_timer_id_t timer_id)
{
	int ret = -1;

	if(self){
		if(timer_id == self->stimers.timer.id){
			ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_timerRefresh, tsk_null, tsk_null);
		}
		else if(timer_id == self->timershutdown.id){
			ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_shutdown_timedout, tsk_null, tsk_null);
		}
	}
	return ret;
}

tsip_dialog_invite_t* tsip_dialog_invite_create(const tsip_ssession_handle_t* ss)
{
	return tsk_object_new(tsip_dialog_invite_def_t,  ss);
}

int tsip_dialog_invite_init(tsip_dialog_invite_t *self)
{
	/* special cases (fsm) should be tried first */
	
	/* 3GPP TS 24.610: Communication Hold  */
	tsip_dialog_invite_hold_init(self);
	/* RFC 4028: Session Timers */
	tsip_dialog_invite_stimers_init(self);
	
	/* Initialize the state machine (all other cases) */
	tsk_fsm_set(TSIP_DIALOG_GET_FSM(self),
	
		/*=======================
		* === Started === 
		*/
		// Started -> (send INVITE) -> Outgoing
		TSK_FSM_ADD_ALWAYS(_fsm_state_Started, _fsm_action_oINVITE, _fsm_state_Outgoing, c0000_Started_2_Outgoing_X_oINVITE, "c0000_Started_2_Outgoing_X_oINVITE"),
		// Started -> (receive INVITE) -> Incoming
		TSK_FSM_ADD_ALWAYS(_fsm_state_Started, _fsm_action_iINVITE, _fsm_state_Incoming, s0000_Started_2_Incoming_X_iINVITE, "s0000_Satrted_2_Incoming_X_iINVITE"),
		// Started -> (Any) -> Started
		TSK_FSM_ADD_ALWAYS_NOTHING(_fsm_state_Started, "tsip_dialog_invite_Started_2_Started_X_any"),

		/*=======================
		* === Outgoing (Client) === 
		*/
		// Outgoing -> (i2xx INVITE) -> Connected
		TSK_FSM_ADD(_fsm_state_Outgoing, _fsm_action_i2xx, _fsm_cond_is_resp2INVITE, _fsm_state_Connected, c0001_Outgoing_2_Connected_X_i2xxINVITE, "c0001_Outgoing_2_Connected_X_i2xxINVITE"),
		// Outgoing -> (300-699 INVITE) -> Termined
		TSK_FSM_ADD(_fsm_state_Outgoing, _fsm_action_i300_to_i699, _fsm_cond_is_resp2INVITE, _fsm_state_Terminated, c0002_Outgoing_2_Terminated_X_i300_to_i699INVITE, "c0002_Outgoing_2_Terminated_X_i300_to_i699INVITE"),
		// Outgoing -> (send CANCEL) -> Terminated
		TSK_FSM_ADD_ALWAYS(_fsm_state_Outgoing, _fsm_action_oCANCEL, _fsm_state_Terminated, c0003_Outgoing_2_Terminated_X_oCANCEL, "c0003_Outgoing_2_Terminated_X_oCANCEL"),

		/*=======================
		* === Connected === 
		*/
		// Connected -> (iINVITE) -> Connected
		TSK_FSM_ADD_ALWAYS(_fsm_state_Connected, _fsm_action_iINVITE, _fsm_state_Connected, x0000_Connected_2_Connected_X_iINVITEorUPDATE, "x0000_Connected_2_Connected_X_iINVITE"),
		// Connected -> (iUPDATE) -> Connected
		TSK_FSM_ADD_ALWAYS(_fsm_state_Connected, _fsm_action_iUPDATE, _fsm_state_Connected, x0000_Connected_2_Connected_X_iINVITEorUPDATE, "x0000_Connected_2_Connected_X_iUPDATE"),

		/*=======================
		* === BYE/SHUTDOWN === 
		*/
		// Any -> (oBYE) -> Trying
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_oBYE, _fsm_state_Trying, x0000_Any_2_Trying_X_oBYE, "x0000_Any_2_Trying_X_oBYE"),
		// Any -> (oBYE) -> Terminated
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_iBYE, _fsm_state_Terminated, x0000_Any_2_Terminated_X_iBYE, "x0000_Any_2_Terminated_X_iBYE"),
		// Trying -> (i2xx BYE) -> Terminated
		TSK_FSM_ADD(_fsm_state_Trying, _fsm_action_i2xx, _fsm_cond_is_resp2BYE, _fsm_state_Terminated, tsk_null, "x0000_Trying_2_Terminated_X_i2xxBYE"),
		// Trying -> (i3xx-i6xx BYE) -> Terminated
		TSK_FSM_ADD(_fsm_state_Trying, _fsm_action_i300_to_i699, _fsm_cond_is_resp2BYE, _fsm_state_Terminated, tsk_null, "x0000_Trying_2_Terminated_X_i2xxTOi6xxBYE"),
		// Any -> (Shutdown) -> Trying
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_oShutdown, _fsm_state_Trying, x0000_Any_2_Trying_X_shutdown, "x0000_Any_2_Trying_X_shutdown"),
		// Any -> (shutdown timedout) -> Terminated
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_shutdown_timedout, _fsm_state_Terminated, tsk_null, "tsip_dialog_invite_shutdown_timedout"),

		
		/*=======================
		* === Any === 
		*/
		// Any -> (i1xx) -> Any
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_i1xx, tsk_fsm_state_any, x0000_Any_2_Any_X_i1xx, "x0000_Any_2_Any_X_i1xx"),
		// Any -> (i401/407)
		//
		// Any -> (iOPTIONS) -> Any
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_iOPTIONS, tsk_fsm_state_any, x0000_Any_2_Any_X_iOPTIONS, "x0000_Any_2_Any_X_iOPTIONS"),
		// Any -> (i2xx INVITE) -> Any
		TSK_FSM_ADD(tsk_fsm_state_any, _fsm_action_i2xx, _fsm_cond_is_resp2INVITE, tsk_fsm_state_any, x0000_Any_2_Any_X_i2xxINVITEorUPDATE, "x0000_Any_2_Any_X_i2xxINVITE"),
		// Any -> (i2xx UPDATE) -> Any
		TSK_FSM_ADD(tsk_fsm_state_any, _fsm_action_i2xx, _fsm_cond_is_resp2UPDATE, tsk_fsm_state_any, x0000_Any_2_Any_X_i2xxINVITEorUPDATE, "x0000_Any_2_Any_X_i2xxUPDATE"),
		// Any -> (i2xx PRACK) -> Any
		TSK_FSM_ADD(tsk_fsm_state_any, _fsm_action_i2xx, _fsm_cond_is_resp2PRACK, tsk_fsm_state_any, tsk_null, "x0000_Any_2_Any_X_i2xxPRACK"),
		// Any -> (transport error) -> Terminated
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_transporterror, _fsm_state_Terminated, x9998_Any_2_Any_X_transportError, "x9998_Any_2_Any_X_transportError"),
		// Any -> (transport error) -> Terminated
		TSK_FSM_ADD_ALWAYS(tsk_fsm_state_any, _fsm_action_error, _fsm_state_Terminated, x9999_Any_2_Any_X_Error, "x9999_Any_2_Any_X_Error"),

		TSK_FSM_ADD_NULL());

	/* Sets callback function */
	TSIP_DIALOG(self)->callback = TSIP_DIALOG_EVENT_CALLBACK_F(tsip_dialog_invite_event_callback);

	/* Timers */
	//self->timerrefresh.id = TSK_INVALID_TIMER_ID;
	//self->timerrefresh.timeout = ;
	self->timershutdown.id = TSK_INVALID_TIMER_ID;
	self->timershutdown.timeout = TSIP_DIALOG_SHUTDOWN_TIMEOUT;

	return 0;
}

// start sending
int tsip_dialog_invite_start(tsip_dialog_invite_t *self)
{
	int ret = -1;
	if(self && !TSIP_DIALOG(self)->running){
		if(!(ret = tsip_dialog_fsm_act(TSIP_DIALOG(self), _fsm_action_oINVITE, tsk_null, tsk_null))){
			TSIP_DIALOG(self)->running = tsk_true;
		}
	}
	return ret;
}

int tsip_dialog_invite_process_ro(tsip_dialog_invite_t *self, const tsip_message_t* message)
{
	tsdp_message_t* sdp_ro = tsk_null;
	int ret = 0;

	if(!self || !message){
		TSK_DEBUG_ERROR("Invalid parameter");
		return -1;
	}

	/* Parse SDP content */
	if(TSIP_MESSAGE_HAS_CONTENT(message)){
		if(tsk_striequals("application/sdp", TSIP_MESSAGE_CONTENT_TYPE(message))){
			if(!(sdp_ro = tsdp_message_parse(TSIP_MESSAGE_CONTENT_DATA(message), TSIP_MESSAGE_CONTENT_DATA_LENGTH(message)))){
				TSK_DEBUG_ERROR("Failed to parse remote sdp message");
				return -2;
			}
		}
		else{
			TSK_DEBUG_ERROR("[%s] content-type is not supportted", TSIP_MESSAGE_CONTENT_TYPE(message));
			return -3;
		}
	}
	else{
		/* Ignore it */
		return 0;
	}

	/* Create session Manager if not already done */
	if(!self->msession_mgr){
		tmedia_type_t type = (tmedia_audio | tmedia_video | tmedia_msrp | tmedia_t38); /* FIXME */
		if(sdp_ro){
			type = tmedia_type_from_sdp(sdp_ro);
		}
		self->msession_mgr = tmedia_session_mgr_create(type, TSIP_DIALOG_GET_STACK(self)->network.local_ip, tsk_false, (sdp_ro == tsk_null));
	}
	
	if(sdp_ro){
		if((ret = tmedia_session_mgr_set_ro(self->msession_mgr, sdp_ro))){
			TSK_DEBUG_ERROR("Failed to set remote offer");
			goto bail;
		}
	}
	
	/* start session manager */
	if(!self->msession_mgr->started && (ret = tmedia_session_mgr_start(self->msession_mgr))){
		TSK_DEBUG_ERROR("Failed to start session manager");
		goto bail;
	}
	
bail:
	TSK_OBJECT_SAFE_FREE(sdp_ro);

	return ret;
}


//--------------------------------------------------------
//				== STATE MACHINE BEGIN ==
//--------------------------------------------------------


int x0000_Connected_2_Connected_X_iINVITEorUPDATE(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_request_t *rINVITEorUPDATE = va_arg(*app, const tsip_request_t *);

	int ret = 0;

	/* process remote offer */
	if((ret = tsip_dialog_invite_process_ro(self, rINVITEorUPDATE))){
		/* Send error */
		return ret;
	}

	/* session timers */
	if(self->stimers.timer.timeout){
		tsip_dialog_invite_stimers_handle(self, rINVITEorUPDATE);
	}

	/* Send 200 OK */
	ret = send_RESPONSE(self, rINVITEorUPDATE, 200, "OK");

	/* alert the user */
	

	return ret;
}

int x0000_Any_2_Any_X_iOPTIONS(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_request_t *rOPTIONS = va_arg(*app, const tsip_request_t *);

	if(!rOPTIONS){
		/* Do not signal error */
		return 0;
	}

	/* Alert user */

	/* Send 2xx */
	send_RESPONSE(self, rOPTIONS, 200, "OK");

	return 0;
}

int x0000_Any_2_Any_X_i2xxINVITEorUPDATE(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_response_t *r2xx = va_arg(*app, const tsip_response_t *);
	int ret = 0;

	/* Update the dialog state */
	if((ret = tsip_dialog_update(TSIP_DIALOG(self), r2xx))){
		return ret;
	}

	/* session timers */
	if(self->stimers.timer.timeout){
		tsip_dialog_invite_stimers_handle(self, r2xx);
	}

	/* send ACK */
	if(TSIP_RESPONSE_IS_TO_INVITE(r2xx)){
		ret = send_ACK(self, r2xx);
	}
	
	return ret;
}


/* Any -> (oBYE) -> Trying */
int x0000_Any_2_Trying_X_oBYE(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);

	/* send BYE */
	return send_BYE(self);
}

/* Any -> (iBYE) -> Terminated */
int x0000_Any_2_Terminated_X_iBYE(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_request_t *rBYE = va_arg(*app, const tsip_request_t *);

	/* send 200 OK */
	return send_RESPONSE(self, rBYE, 200, "OK");
}

int x0000_Any_2_Trying_X_shutdown(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	
	/* schedule shutdow timeout */
	TSIP_DIALOG_INVITE_TIMER_SCHEDULE(shutdown);

	/* alert user */
	TSIP_DIALOG_SIGNAL(self, tsip_event_code_dialog_terminating, "Terminating dialog");
	
	if(TSIP_DIALOG(self)->state == tsip_established){
		return send_BYE(self);
	}
	else if(TSIP_DIALOG(self)->state == tsip_early){
		return send_CANCEL(self);
	}
	
	return 0;
}

int x0000_Any_2_Trying_X_oREINVITE(va_list *app)
{
	return 0;
}

int x0000_Any_2_Trying_X_oUPDATE(va_list *app)
{
	return 0;
}

int x0000_Any_2_Any_X_iREINVITE(va_list *app)
{
	return 0;
}

int x0000_Any_2_Any_X_iUPDATE(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_request_t *rUPDATE = va_arg(*app, const tsip_request_t *);

	int ret;
	
	/* Send 200 OK */
	ret = send_RESPONSE(self, rUPDATE, 200, "OK");

	/* alert the user */

	return ret;
}

int x0000_Any_2_Any_X_i1xx(va_list *app)
{
	tsip_dialog_invite_t *self = va_arg(*app, tsip_dialog_invite_t *);
	const tsip_response_t *r1xx = va_arg(*app, const tsip_response_t *);
	int ret = 0;

	/* Update the dialog state */
	if((ret = tsip_dialog_update(TSIP_DIALOG(self), r1xx))){
		return ret;
	}

	/* RFC 3262 - 4 UAC Behavior
		If a provisional response is received for an initial request, and
		that response contains a Require header field containing the option
		tag 100rel, the response is to be sent reliably.  If the response is
		a 100 (Trying) (as opposed to 101 to 199), this option tag MUST be
		ignored, and the procedures below MUST NOT be used.

		Assuming the response is to be transmitted reliably, the UAC MUST
		create a new request with method PRACK.  This request is sent within
		the dialog associated with the provisional response (indeed, the
		provisional response may have created the dialog).  PRACK requests
		MAY contain bodies, which are interpreted according to their type and
		disposition.

		Note that the PRACK is like any other non-INVITE request within a
		dialog.  In particular, a UAC SHOULD NOT retransmit the PRACK request
		when it receives a retransmission of the provisional response being
		acknowledged, although doing so does not create a protocol error.
	*/
	if((TSIP_RESPONSE_CODE(r1xx) >= 101 && TSIP_RESPONSE_CODE(r1xx) <=199) && tsip_message_required(r1xx, "100rel")){
		/* Process Remote offer */
		if((ret = tsip_dialog_invite_process_ro(self, r1xx))){
			/* Send Error */
			return ret;
		}

		if((ret = send_PRACK(self, r1xx))){
			return ret;
		}
	}

	return ret;
}

int x0000_Any_2_Any_X_i2xx(va_list *app)
{
	/* FIXME: if 2xxINVITE, then send ACK */
	return 0;
}

int x0000_Any_2_Any_X_i401_i407_i421_i494(va_list *app)
{
	return 0;
}

int x0000_Any_2_Any_X_iPRACK(va_list *app)
{
	return 0;
}

int x0000_Any_2_Any_X_iACK(va_list *app)
{
	return 0;
}

int x9998_Any_2_Any_X_transportError(va_list *app)
{
	return 0;
}

int x9999_Any_2_Any_X_Error(va_list *app)
{
	return 0;
}


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//				== STATE MACHINE END ==
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++


// send INVITE/UPDATE request
int send_INVITEorUPDATE(tsip_dialog_invite_t *self, tsk_bool_t is_INVITE)
{
	int ret = -1;
	tsip_request_t *request = tsk_null;

	if(!self){
		TSK_DEBUG_ERROR("Invalid parameter");
		goto bail;
	}

	if((request = tsip_dialog_request_new(TSIP_DIALOG(self), is_INVITE ? "INVITE" : "UPDATE"))){

		/* apply action params to the request (will add a content if the action contains one) */
		if(TSIP_DIALOG(self)->curr_action){
			tsip_dialog_apply_action(request, TSIP_DIALOG(self)->curr_action);
		}
		
		/* add our payload if current action does not have one */
		if(!TSIP_DIALOG(self)->curr_action || !TSIP_DIALOG(self)->curr_action->payload){
			const tsdp_message_t* sdp_lo;
			char* sdp;
			if((sdp_lo = tmedia_session_mgr_get_lo(self->msession_mgr)) && (sdp = tsdp_message_tostring(sdp_lo))){
				tsip_message_add_content(request, "application/sdp", sdp, tsk_strlen(sdp));
				TSK_FREE(sdp);
			}
		}

		/* Session timers */
		if(self->stimers.timer.timeout){
			tsip_message_add_headers(request,
					TSIP_HEADER_SESSION_EXPIRES_VA_ARGS(self->stimers.timer.timeout, tsk_striequals(self->stimers.refresher, "uas")),
					TSIP_HEADER_SUPPORTED_VA_ARGS("timer"),
					tsk_null
				);
		}
		if(self->stimers.minse){
			tsip_message_add_headers(request,
					TSIP_HEADER_MIN_SE_VA_ARGS(self->stimers.minse),
					tsk_null
				);
		}
		
		/* 100rel */
		if(self->enable_100rel){
			tsip_message_add_headers(request,
					TSIP_HEADER_SUPPORTED_VA_ARGS("100rel"),
					tsk_null
				);
		}

		/* send the request */
		ret = tsip_dialog_request_send(TSIP_DIALOG(self), request);
		if(ret == 0){
			/* update last INVITE */
			TSK_OBJECT_SAFE_FREE(self->last_invite);
			self->last_invite = request;
		}
		else{
			TSK_OBJECT_SAFE_FREE(request);
		}
	}

bail:
	return ret;
}

int send_PRACK(tsip_dialog_invite_t *self, const tsip_response_t* r1xx)
{
	// "Require: 100rel\r\n" should be checked by the caller of this function
	int ret = -1;
	tsip_request_t *request = tsk_null;
	const tsip_header_RSeq_t* RSeq;

	if(!self || !r1xx || !r1xx->CSeq){
		TSK_DEBUG_ERROR("Invalid parameter");
		goto bail;
	}

	/* RFC 3262 - 4 UAC Behavior
	Assuming the response is to be transmitted reliably, the UAC MUST
	create a new request with method PRACK.
	*/
	if(!(request = tsip_dialog_request_new(TSIP_DIALOG(self), "PRACK"))){
		goto bail;
	}




	/*	RFC 3262 - 4 UAC Behavior
	The UAC MUST maintain a sequence number that indicates the most recently
	received in-order reliable provisional response for the initial request.
	*/
	if((RSeq = (const tsip_header_RSeq_t*)tsip_message_get_header(r1xx, tsip_htype_RSeq))){

		/*	RFC 3262 - 4 UAC Behavior
		If the UAC receives another reliable provisional
		response to the same request, and its RSeq value is not one higher
		than the value of the sequence number, that response MUST NOT be
		acknowledged with a PRACK, and MUST NOT be processed further by the
		UAC.  An implementation MAY discard the response, or MAY cache the
		response in the hopes of receiving the missing responses.
		*/
		if(self->rseq && (RSeq->seq <= self->rseq)){
			TSK_DEBUG_WARN("1xx.RSeq value is not one higher than lastINVITE.RSeq.");
			ret = 0; /* Not error */
			goto bail;
		}
		self->rseq = RSeq->seq;
	}

	/* RFC 3262 - 7.2 RAck
		The first number is the value from the RSeq header in the provisional
		response that is being acknowledged.  The next number, and the
		method, are copied from the CSeq in the response that is being
		acknowledged.  The method name in the RAck header is case sensitive.
	*/
	TSIP_MESSAGE_ADD_HEADER(request, TSIP_HEADER_RACK_VA_ARGS(self->rseq, r1xx->CSeq->seq, r1xx->CSeq->method));

	ret = tsip_dialog_request_send(TSIP_DIALOG(self), request);
	
bail:
	TSK_OBJECT_SAFE_FREE(request);
	return ret;
}

int send_CANCEL(tsip_dialog_invite_t *self)
{
	int ret = -1;
	if(!self){
		TSK_DEBUG_ERROR("Invalid parameter");
		goto bail;
	}
	/* RFC 3261 - 9 Canceling a Request
		If the request being cancelled contains a Route header field, the
		CANCEL request MUST include that Route header field's values.
		==> up to tsip_dialog_request_new()
	*/

	/*	RFC 3261 - 9 Canceling a Request
		Once the CANCEL is constructed, the client SHOULD check whether it
		has received any response (provisional or final) for the request
		being cancelled (herein referred to as the "original request").

		If no provisional response has been received, the CANCEL request MUST
		NOT be sent; rather, the client MUST wait for the arrival of a
		provisional response before sending the request.
		==> up to the caller to check that we are not in the initial state and the FSM
		is in Trying state.
   */

	/*	RFC 3261 - 9 Canceling a Request
		The following procedures are used to construct a CANCEL request.  The
		Request-URI, Call-ID, To, the numeric part of CSeq, and From header
		fields in the CANCEL request MUST be identical to those in the
		request being cancelled, including tags.  A CANCEL constructed by a
		client MUST have only a single Via header field value matching the
		top Via value in the request being cancelled.  Using the same values
		for these header fields allows the CANCEL to be matched with the
		request it cancels (Section 9.2 indicates how such matching occurs).
		However, the method part of the CSeq header field MUST have a value
		of CANCEL.  This allows it to be identified and processed as a
		transaction in its own right (See Section 17)
	*/
	if(self->last_invite){
		/* to avoid concurrent access, take a reference to the request */
		tsip_request_t* last_invite = tsk_object_ref(self->last_invite);
		tsip_request_t* cancel;
		if((cancel = tsip_dialog_request_new(TSIP_DIALOG(self), "CANCEL"))){
			/* Request-URI, Call-ID, To and From will be added by the dialog layer */
			/* the numeric part of CSeq */
			cancel->CSeq->seq = last_invite->CSeq->seq;
			/* Via */
			TSK_OBJECT_SAFE_FREE(cancel->firstVia); /* firstVia is already Null ...but who know? */
			cancel->firstVia = tsk_object_ref(last_invite->firstVia); /* transport layer won't update the Via header */

			ret = tsip_dialog_request_send(TSIP_DIALOG(self), cancel);
			TSK_OBJECT_SAFE_FREE(cancel);
		}
		else{
			TSK_DEBUG_ERROR("Failed to create CANCEL request");
			ret = -2;
		}

		tsk_object_unref(last_invite);
		return ret;
	}
	else{
		TSK_DEBUG_WARN("There is no INVITE request to cancel");
		return 0;
	}

bail:
	return ret;
}

int send_BYE(tsip_dialog_invite_t *self)
{
	int ret = -1;
	tsip_request_t *bye = tsk_null;

	if(!self){
		TSK_DEBUG_ERROR("Invalid parameter");
		goto bail;
	}
	/* RFC 3261 - 15.1.1 UAC Behavior
		A BYE request is constructed as would any other request within a
		dialog, as described in Section 12.

		Once the BYE is constructed, the UAC core creates a new non-INVITE
		client transaction, and passes it the BYE request.  The UAC MUST
		consider the session terminated (and therefore stop sending or
		listening for media) as soon as the BYE request is passed to the
		client transaction.  If the response for the BYE is a 481
		(Call/Transaction Does Not Exist) or a 408 (Request Timeout) or no

		response at all is received for the BYE (that is, a timeout is
		returned by the client transaction), the UAC MUST consider the
		session and the dialog terminated.
	*/
	if((bye = tsip_dialog_request_new(TSIP_DIALOG(self), "BYE"))){
		ret = tsip_dialog_request_send(TSIP_DIALOG(self), bye);
		TSK_OBJECT_SAFE_FREE(bye);
	}

bail:
	return ret;
}

int send_ACK(tsip_dialog_invite_t *self, const tsip_response_t* r2xxINVITE)
{
	int ret = -1;
	tsip_request_t *request = tsk_null;

	if(!self){
		TSK_DEBUG_ERROR("Invalid parameter");
		goto bail;
	}	

	if((request = tsip_dialog_request_new(TSIP_DIALOG(self), "ACK"))){

		/*	RFC 3261 - 13.2.2.4 2xx Responses
		   The UAC core MUST generate an ACK request for each 2xx received from
		   the transaction layer.  The header fields of the ACK are constructed
		   in the same way as for any request sent within a dialog (see Section
		   12) with the exception of the CSeq and the header fields related to
		   authentication.  The sequence number of the CSeq header field MUST be
		   the same as the INVITE being acknowledged, but the CSeq method MUST
		   be ACK.  The ACK MUST contain the same credentials as the INVITE.  If
		   the 2xx contains an offer (based on the rules above), the ACK MUST
		   carry an answer in its body.  If the offer in the 2xx response is not
		   acceptable, the UAC core MUST generate a valid answer in the ACK and
		   then send a BYE immediately.
		   ==> Credentials will be added by tsip_dialog_request_new() because they are
		   associated to the dialog itself.
		   ==> It's up to us to add/update the CSeq number.
		   ==> ACK requests sent here will create new client transactions, which means that
		   they will have there own branches. This is not the case for ACK requests sent from
		   the transaction layer.
		*/
		request->CSeq->seq = r2xxINVITE->CSeq->seq; /* As the 2xx has the same CSeq than the INVITE */
		
		/*	RFC 3261 - 13.2.2.4 2xx Responses
		   Once the ACK has been constructed, the procedures of [4] are used to
		   determine the destination address, port and transport.  However, the
		   request is passed to the transport layer directly for transmission,
		   rather than a client transaction.  This is because the UAC core
		   handles retransmissions of the ACK, not the transaction layer.  The
		   ACK MUST be passed to the client transport every time a
		   retransmission of the 2xx final response that triggered the ACK
		   arrives.
		*/
		if(TSIP_DIALOG_GET_STACK(self)->layer_transport){
			ret = tsip_transport_layer_send(TSIP_DIALOG_GET_STACK(self)->layer_transport, tsk_null, request);
		}
		else{
			ret = -1;
			TSK_DEBUG_ERROR("Not Transport layer associated to this stack");
		}
		TSK_OBJECT_SAFE_FREE(request);
	}

bail:
	return ret;
}

int send_RESPONSE(tsip_dialog_invite_t *self, const tsip_request_t* request, short code, const char* phrase)
{
	tsip_response_t *response;
	int ret = -1;

	if((response = tsip_dialog_response_new(TSIP_DIALOG(self), code, phrase, request))){
		if(TSIP_REQUEST_IS_INVITE(request) || TSIP_REQUEST_IS_UPDATE(request)){
			/* Session timers (for 2xx to INVITE or UPDATE) */
			if(self->stimers.timer.timeout){
				tsip_message_add_headers(response,
						TSIP_HEADER_SESSION_EXPIRES_VA_ARGS(self->stimers.timer.timeout, tsk_striequals(self->stimers.refresher, "uas")),
						tsk_null
					);
			}
			if(self->stimers.minse){
				tsip_message_add_headers(response,
						TSIP_HEADER_MIN_SE_VA_ARGS(self->stimers.minse),
						tsk_null
					);
			}
		}

		ret = tsip_dialog_response_send(TSIP_DIALOG(self), response);
		TSK_OBJECT_SAFE_FREE(response);
	}
	return ret;
}

int tsip_dialog_invite_OnTerminated(tsip_dialog_invite_t *self)
{
	TSK_DEBUG_INFO("=== INVITE Dialog terminated ===");

	/* stop session manager */
	if(self->msession_mgr && self->msession_mgr->started){
		tmedia_session_mgr_stop(self->msession_mgr);
	}

	/* alert the user */
	TSIP_DIALOG_SIGNAL(self, tsip_event_code_dialog_terminated, "Dialog terminated");

	/* Remove from the dialog layer. */
	return tsip_dialog_remove(TSIP_DIALOG(self));
}






















//========================================================
//	SIP dialog INVITE object definition
//
static tsk_object_t* tsip_dialog_invite_ctor(tsk_object_t * self, va_list * app)
{
	tsip_dialog_invite_t *dialog = self;
	if(dialog){
		tsip_ssession_handle_t *ss = va_arg(*app, tsip_ssession_handle_t *);

		/* Initialize base class */
		tsip_dialog_init(TSIP_DIALOG(self), tsip_dialog_INVITE, tsk_null, ss, _fsm_state_Started, _fsm_state_Terminated);

		/* FSM */
		TSIP_DIALOG_GET_FSM(dialog)->debug = DEBUG_STATE_MACHINE;
		tsk_fsm_set_callback_terminated(TSIP_DIALOG_GET_FSM(dialog), TSK_FSM_ONTERMINATED_F(tsip_dialog_invite_OnTerminated), (const void*)dialog);

		/* Initialize the class itself */
		tsip_dialog_invite_init(self);
	}
	return self;
}

static tsk_object_t* tsip_dialog_invite_dtor(tsk_object_t * _self)
{ 
	tsip_dialog_invite_t *self = _self;
	if(self){
		/* Cancel all timers */
		tsip_dialog_invite_stimers_cancel(self);
		TSIP_DIALOG_TIMER_CANCEL(shutdown);

		/* DeInitialize base class */
		tsip_dialog_deinit(TSIP_DIALOG(self));
		
		/* DeInitialize self */
		TSK_OBJECT_SAFE_FREE(self->msession_mgr);
		TSK_OBJECT_SAFE_FREE(self->last_invite);
		TSK_FREE(self->stimers.refresher);
		//...

		TSK_DEBUG_INFO("*** INVITE Dialog destroyed ***");
	}
	return self;
}

static int tsip_dialog_invite_cmp(const tsk_object_t *obj1, const tsk_object_t *obj2)
{
	return tsip_dialog_cmp(obj1, obj2);
}

static const tsk_object_def_t tsip_dialog_invite_def_s = 
{
	sizeof(tsip_dialog_invite_t),
	tsip_dialog_invite_ctor, 
	tsip_dialog_invite_dtor,
	tsip_dialog_invite_cmp, 
};
const tsk_object_def_t *tsip_dialog_invite_def_t = &tsip_dialog_invite_def_s;