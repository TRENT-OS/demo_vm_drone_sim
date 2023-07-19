/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */
 
 
#include "lib_debug/Debug.h"
#include <string.h>

#include "OS_Socket.h"
#include "interfaces/if_OS_Socket.h"

#include "lib_macros/Check.h"
#include "lib_macros/Test.h"
#include <arpa/inet.h>

#include "mavlink_filter/mavlink_filter.h"
#include "libs/util/socket_helper.h"

#include <camkes.h>



//----------------------------------------------------------------------
// Context
//----------------------------------------------------------------------


void socket_VM_event_callback(void* ctx);
void socket_PX4_event_callback(void* ctx);


// VM       <--> TRENTOS
socket_ctx_t socket_VM = {
    .socket = IF_OS_SOCKET_ASSIGN(socket_VM_nws),
    .addr = {
        .addr = VM_TRENTOS_ADDR,
        .port = VM_TRENTOS_PORT
    },
    .callback = socket_VM_event_callback,
    .callback_ctx = &socket_VM,
    .conn_init = false,
};


// TRENTOS  <--> PX4(Linux Host)
socket_ctx_t socket_PX4 = {
    .socket     = IF_OS_SOCKET_ASSIGN(socket_PX4_nws),
    .addr       = {
        .addr = PX4_TRENTOS_ADDR,
        .port = PX4_TRENTOS_PORT
    },
    .callback = socket_PX4_event_callback,
    .callback_ctx = &socket_PX4,
    .addr_partner = {
        .addr = PX4_DRONE_ADDR,
        .port = PX4_DRONE_PORT
    },
    .conn_init = false,
};



//----------------------------------------------------------------------
// Callback PX4
//----------------------------------------------------------------------


void socket_PX4_event_callback(void* ctx) 
{
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = ctx; 
    Debug_ASSERT(socket_from != &socket_VM);
    socket_ctx_t * socket_to = &socket_VM;
    
    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = {0};
    int numberOfSocketsWithEvents = 0;
    size_t eventBufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
                         &socket_from->socket,
                         eventBuffer,
                         eventBufferSize,
                         &numberOfSocketsWithEvents); 
    ASSERT_EQ_OS_ERR(OS_SUCCESS, err);
    if (err) {
        Debug_LOG_ERROR("failed to retrieve pending events. Error: %i", err);
        return;
    }
    
    // Verify that the received number of sockets with events is within expected
    // bounds.
    ASSERT_LE_INT(numberOfSocketsWithEvents, OS_NETWORK_MAXIMUM_SOCKET_NO);
    ASSERT_GT_INT(numberOfSocketsWithEvents, -1);

    for (int i = 0; i < numberOfSocketsWithEvents; i++) {
        OS_Socket_Evt_t event;
        memcpy(&event, &eventBuffer[i], sizeof(event));

        err = SharedResourceMutex_lock();
        if (err) {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }

        uint8_t eventMask = event.eventMask;
        if (eventMask & OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN) {
            err = OS_Socket_close(socket_from->handle);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
            socket_from->conn_init = false;
            return;
        } else if (eventMask & OS_SOCK_EV_CONN_EST) {
            socket_from->conn_init = true;
            Debug_LOG_ERROR("PX4 socket connection established");
        } else if (eventMask & OS_SOCK_EV_READ) {
            char buf[4096] = { 0 };
            size_t len_requested = sizeof(buf);
            size_t len_actual = 0;

            err = OS_Socket_read(
                socket_from->handle,
                buf,
                len_requested,
                &len_actual);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_read() failed, code %d", err);
                goto reset_PX4;
            }

            // Check if partner socket is ready to send
            if (!socket_to->conn_init) {
                goto reset_PX4;
            }
            
            err = OS_Socket_write(
                socket_to->client_handle,
                buf,
                len_requested,
                &len_actual
            );
            if (err) {
                Debug_LOG_ERROR("OS_Socket_write() failed, code %d", err);
            }
        }

reset_PX4:
        memset(&eventBuffer[event.socketHandle], 0, sizeof(OS_Socket_Evt_t));
        err = SharedResourceMutex_unlock();
        if (err) {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    //register socket callback
    if ((err = OS_Socket_regCallback(
              &socket_from->socket,
              socket_from->callback,
              socket_from->callback_ctx))) {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}



//----------------------------------------------------------------------
// Callback VM
//----------------------------------------------------------------------


void socket_VM_event_callback(void* ctx)
{
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = ctx; 
    Debug_ASSERT(socket_from != &socket_PX4);
    socket_ctx_t * socket_to = &socket_PX4;

    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = { 0 };
    int numberOfSocketsWithEvents = 0;
    size_t eventBufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
                         &socket_from->socket,
                         eventBuffer,
                         eventBufferSize,
                         &numberOfSocketsWithEvents);
    if (err) {
        Debug_LOG_ERROR("failed to retrieve pending events. Error: %i", err);
        return;
    }

    // Verify that the received number of sockets with events is within expected
    // bounds.
    ASSERT_LE_INT(numberOfSocketsWithEvents, OS_NETWORK_MAXIMUM_SOCKET_NO);
    ASSERT_GT_INT(numberOfSocketsWithEvents, -1);

    for (int i = 0; i < numberOfSocketsWithEvents; i++) {
        OS_Socket_Evt_t event;
        memcpy(&event, &eventBuffer[i], sizeof(OS_Socket_Evt_t));
        
        err = SharedResourceMutex_lock();
        if (err)
        {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }
        
        if (!(event.socketHandle >= 0 && 
              event.socketHandle < OS_NETWORK_MAXIMUM_SOCKET_NO)) {
            Debug_LOG_ERROR("Found invalid socket handle %d for event %d", event.socketHandle, i);
            goto reset_VM;
        }        

        uint8_t eventMask = event.eventMask;
        
        if (eventMask & OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN) {
            Debug_LOG_ERROR("Received event: OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN");
            err = OS_Socket_close(socket_from->handle);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
            socket_from->conn_init = false;
            return;
        }

        if (eventMask & OS_SOCK_EV_CONN_ACPT) {
            Debug_LOG_ERROR("Conn accpt event");
            err = OS_Socket_accept(socket_from->handle, &socket_from->client_handle, &socket_from->addr_partner);
            if (err == OS_ERROR_TRY_AGAIN) {
                Debug_LOG_ERROR("Socket accept failed OS_ERROR_TRY_AGAIN");
                
                goto reset_VM;
            } else if (err) {
                Debug_LOG_ERROR("OS_Socket_accept() failed, error %d", err);
                OS_Socket_close(socket_from->handle);
                goto reset_VM;
            }
            socket_from->conn_init = true;

            if ((err = init_socket_nb_client(&socket_PX4))) {
                Debug_LOG_ERROR("Initialization of the px4 socket failed. code: %d", err);
                return;
            }
            Debug_LOG_ERROR("PX4 socket succesfully initialized.");

            printf("Set VM IP address to: IP: %s PORT: %d\n", socket_from->addr_partner.addr, ntohs(socket_from->addr_partner.port));
        }  else if (eventMask & OS_SOCK_EV_READ) {
            //Debug_LOG_ERROR("READ EVENT VM");
            static char buf[1500] = { 0 };
            static char ret_buf[1500] = { 0 };
            size_t ret_len = 0;
            size_t len_requested = sizeof(buf);
            size_t len_actual = 0;
            
            err = OS_Socket_read(
                socket_from->client_handle,
                buf,
                len_requested,
                &len_actual);

            if (err) {
                Debug_LOG_ERROR("OS_Socket_read() failed, code %d", err);
                goto reset_VM;
            }

            //Applying filter to data from VM -> PX4
            filter_mavlink_message(buf, &len_actual, ret_buf, &ret_len);

            Debug_LOG_TRACE("Len of packet prior to filetering: %lu Len now: %lu\n", len_actual, ret_len);

            // Check if partner socket is ready to send
            if (!socket_to->conn_init) {
                    Debug_LOG_ERROR("Dropping Packet: Socket_VM notinitialized yet");
                goto reset_VM;
            }
            
            if (ret_len) {
                //send buffer with the filtered messages to PX4
                err = OS_Socket_write(socket_to->handle, ret_buf, ret_len, &len_actual);
                if (err) {
                    Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
                }
            }
        } else {
            //Debug_LOG_ERROR("Received unhandled event!"); TODO: Handle
        }
        
reset_VM:
        memset(&eventBuffer[event.socketHandle], 0, sizeof(OS_Socket_Evt_t));
        err = SharedResourceMutex_unlock();
        if (err) {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    //register socket callback
    if ((err = OS_Socket_regCallback(
              &socket_from->socket,
              socket_from->callback,
              socket_from->callback_ctx))) {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}


//----------------------------------------------------------------------
// Camkes init functions
//----------------------------------------------------------------------


void post_init(void)
{
    int backlog = 10;
    if (init_socket_nb_server(&socket_VM, backlog)) {
        Debug_LOG_ERROR("Initialization of the VM socket failed");
        return;
    }
    Debug_LOG_ERROR("Init done");
}
