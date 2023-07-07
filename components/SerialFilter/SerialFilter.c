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

#include "libs/mavlink_filter/mavlink_filter.h"

#include <camkes.h>



//----------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------


typedef void (*callbackFunc_t)(void*);


typedef struct {
    const if_OS_Socket_t    socket;
    const OS_Socket_Addr_t  addr;
    OS_Socket_Addr_t        addr_partner;
    bool                    conn_init;
    callbackFunc_t          callback; 
    OS_Socket_Handle_t      handle;
    OS_Socket_Handle_t      client_handle;
} socket_ctx_t;


void socket_VM_event_callback(void* ctx);
void socket_PX4_event_callback(void* ctx);



//----------------------------------------------------------------------
// State
//----------------------------------------------------------------------


// VM       <--> TRENTOS
socket_ctx_t socket_VM = {
    .socket = IF_OS_SOCKET_ASSIGN(socket_VM_nws),
    .addr = {
        .addr = VM_TRENTOS_ADDR,
        .port = VM_TRENTOS_PORT
    },
    .callback = socket_VM_event_callback,
    .conn_init = false,
};


// TRENTOS  <--> PX4(Linux Host)
socket_ctx_t socket_PX4 = {
    .socket     = IF_OS_SOCKET_ASSIGN(socket_PX4_nws),
    .addr       = {
        .addr = PX4_TRENTOS_ADDR,
        .port = PX4_TRENTOS_PORT
    },
    .addr_partner = {
        .addr = PX4_DRONE_ADDR,
        .port = PX4_DRONE_PORT
    },
    .callback = socket_PX4_event_callback,
    .conn_init = false,
};



//----------------------------------------------------------------------
// Init helper functions
//----------------------------------------------------------------------


OS_Error_t wait_for_nw_stack_init(const if_OS_Socket_t * const nw_sock) {
    OS_NetworkStack_State_t networkStackState;
    do {
        networkStackState = OS_Socket_getStatus(nw_sock);
        if (networkStackState == UNINITIALIZED || networkStackState == INITIALIZED) {
            seL4_Yield();
        }

        if (networkStackState == FATAL_ERROR) {
            Debug_LOG_ERROR("A FATAL_ERROR occurred in the Network Stack component.");
            return OS_ERROR_ABORTED;
        }
    } while (networkStackState != RUNNING);
    return OS_SUCCESS;
}


OS_Error_t init_socket(socket_ctx_t *socket_ctx) {
    OS_Error_t err;

    //Wait for the Networkstack to be initialized
    err = wait_for_nw_stack_init(&(socket_ctx->socket));
    if (err) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return err;
    }

    //create socket  
    err = OS_Socket_create(
        &(socket_ctx->socket),
        &(socket_ctx->handle),
        OS_AF_INET,
        OS_SOCK_STREAM
    );
    if (err) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return err;
    }

    //register socket callback
    err = OS_Socket_regCallback(
              &(socket_ctx->socket),
              socket_ctx->callback,
              (void*) socket_ctx);
    if (err)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }

    
    return err;
}



//----------------------------------------------------------------------
// Callback PX4
//----------------------------------------------------------------------


void socket_PX4_event_callback(void* ctx) 
{
    //Debug_LOG_ERROR("Socket PX4 callback");
    Debug_ASSERT(NULL != ctx);
    
    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = {0};
    int numberOfSocketsWithEvents = 0;
    size_t eventBufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
                         &socket_PX4.socket,
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
        if (err)
        {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }

        uint8_t eventMask = event.eventMask;
        if (eventMask & OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN) {
            err = OS_Socket_close(socket_PX4.handle);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
            socket_PX4.conn_init = false;
            return;
        } else if (eventMask & OS_SOCK_EV_CONN_EST) {
            socket_PX4.conn_init = true;
            Debug_LOG_ERROR("PX4 socket connection established");
        } else if (eventMask & OS_SOCK_EV_READ) {
            char buf[4096] = { 0 };
            size_t len_requested = sizeof(buf);
            size_t len_actual = 0;

            err = OS_Socket_read(
                socket_PX4.handle,
                buf,
                len_requested,
                &len_actual);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_read() failed, code %d", err);
                goto reset_PX4;
            }


            // Check if partner socket is ready to send
            if (!socket_VM.conn_init) {
                //Debug_LOG_ERROR("Dropping Packet: Socket_VM not initialized yet");
                goto reset_PX4;
            }
            

            err = OS_Socket_write(
                socket_VM.client_handle,
                buf,
                len_requested,
                &len_actual
            );
            if (err) {
                Debug_LOG_ERROR("OS_Socket_write() failed, code %d", err);
            }
        } else {
            //Debug_LOG_ERROR("SocketPx4callback: unknown event received");
        }
reset_PX4:
        memset(&eventBuffer[event.socketHandle], 0, sizeof(OS_Socket_Evt_t));
        err = SharedResourceMutex_unlock();
        if (err)
        {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    err = OS_Socket_regCallback(
        &socket_PX4.socket,
        socket_PX4.callback,
        (void*) &socket_PX4
    );
    if (err)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}



//----------------------------------------------------------------------
// Socket init PX4
//----------------------------------------------------------------------


void socket_init_PX4(socket_ctx_t *socket_ctx) {
    OS_Error_t err;

    err = init_socket(socket_ctx);
    if (err) {
        Debug_LOG_ERROR("Socket initialization failed exiting...");
        exit(-1);
    }

    err = OS_Socket_connect(socket_ctx->handle, &(socket_ctx->addr_partner));
    if (err) {
        Debug_LOG_ERROR("OS_Socket_connect() failed, code %d", err);
        return;
    }
    
    Debug_LOG_ERROR("Init successfull PX4");
}



//----------------------------------------------------------------------
// Callback VM
//----------------------------------------------------------------------


void socket_VM_event_callback(void* ctx)
{
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = &socket_VM; 
    Debug_ASSERT(socket_from != &socket_PX4);
    socket_ctx_t * socket_to = &socket_PX4;

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
        bool cond = event.socketHandle >= 0 && event.socketHandle < OS_NETWORK_MAXIMUM_SOCKET_NO;
        if (!cond) {
            Debug_LOG_ERROR("Found invalid socket handle %d for event %d", event.socketHandle, i);
            goto reset_VM;
        }

        err = SharedResourceMutex_lock();
        if (err)
        {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
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

        if (event.currentError) {
            Debug_LOG_ERROR("Event struct shows error: %d", event.currentError);
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
            socket_VM.conn_init = true;
            socket_init_PX4(&socket_PX4);
            printf("Set VM IP address to: IP: %s PORT: %d\n", socket_from->addr_partner.addr, ntohs(socket_from->addr_partner.port));
        }  else if (eventMask & OS_SOCK_EV_READ) {
            //Debug_LOG_ERROR("READ EVENT VM");
            char buf[4096] = { 0 };
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
            if (filter_mavlink_message(buf, len_actual)) {
                Debug_LOG_ERROR("Packet dropped: violation of filter rules");
                goto reset_VM;
            }

            // Check if partner socket is ready to send
            if (!socket_PX4.conn_init) {
                    Debug_LOG_ERROR("Dropping Packet: Socket_VM notinitialized yet");
                goto reset_VM;
            }
                
            err = OS_Socket_write(socket_to->handle, buf, len_actual, &len_actual);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
            }
           
        } else {
            //Debug_LOG_ERROR("Received unhandled event!"); TODO: Handle
        }
reset_VM:
        memset(&eventBuffer[event.socketHandle], 0, sizeof(OS_Socket_Evt_t));
        err = SharedResourceMutex_unlock();
        if (err)
        {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    err = OS_Socket_regCallback(
        &socket_VM.socket,
        socket_VM.callback,
        (void*) &socket_VM
    );
    if (err)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}



//----------------------------------------------------------------------
// Socket init
//----------------------------------------------------------------------


void socket_init_VM(socket_ctx_t *socket_ctx) {
    OS_Error_t err;

    err = init_socket(socket_ctx);
    if (err) {
        Debug_LOG_ERROR("Socket initialization failed exiting...");
        exit(-1);
    }

    //bind the socket to the address
    err = OS_Socket_bind(socket_ctx->handle, &(socket_ctx->addr));
    if (err) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(socket_ctx->handle);
        if (err)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }

    int backlog = 10;
    //listen on socket
    err = OS_Socket_listen(
        socket_ctx->handle,
        backlog
    );
    if (err) {
        Debug_LOG_ERROR("OS_Socket_listen() failed, code %d", err);
        OS_Socket_close(socket_ctx->handle);
        // TODO: check if reset ev struct is required.
        //nb_helper_reset_ev_struct_for_socket(srvHandle);
        return;
    }
    Debug_LOG_ERROR("Init successfull");
}



//----------------------------------------------------------------------
// Camkes init functions
//----------------------------------------------------------------------


void post_init(void)
{
    
    socket_init_VM(&socket_VM);
    Debug_LOG_ERROR("Init done");
}
