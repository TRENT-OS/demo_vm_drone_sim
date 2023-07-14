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

#include <camkes.h>



//----------------------------------------------------------------------
// Defines
//----------------------------------------------------------------------


typedef struct {
    // Receive: PX4(Linux Host) -> Trentos(SimCoupler)
    const if_OS_Socket_t    socket_px4_receive;
    OS_Socket_Handle_t      handle_px4_receive;
    OS_Socket_Handle_t      client_handle_px4;
    const OS_Socket_Addr_t  addr_px4_trentos;
    OS_Socket_Addr_t        addr_px4_linux;
    bool                    conn_px4_init;

    // Send: Trentos(SimCoupler) -> VM
    const if_OS_Socket_t    socket_vm_send;
    OS_Socket_Handle_t      handle_vm_send;
    OS_Socket_Handle_t      client_handle_vm;
    const OS_Socket_Addr_t  addr_vm_trentos;
    OS_Socket_Addr_t        addr_vm_linux;
    bool                    conn_vm_init;
} simcoupler_ctx_t;


void socket_px4_receive_event_callback(void* ctx);



//----------------------------------------------------------------------
// State
//----------------------------------------------------------------------


simcoupler_ctx_t simcoupler_ctx = {
    .socket_px4_receive = IF_OS_SOCKET_ASSIGN(socket_PX4_nws),
    .addr_px4_trentos = {
        .addr = PX4_TRENTOS_ADDR,
        .port = PX4_TRENTOS_PORT_SIMCOUPLER
    },
    .socket_vm_send = IF_OS_SOCKET_ASSIGN(socket_VM_nws),
    .addr_vm_trentos = {
        .addr = VM_TRENTOS_ADDR,
        .port = VM_TRENTOS_PORT_SIMCOUPLER
    },
    .addr_vm_linux = {
        .addr = VM_LINUX_ADDR,
        .port = VM_LINUX_PORT_SIMCOUPLER
    }
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



//----------------------------------------------------------------------
// Callback
//----------------------------------------------------------------------


void socket_px4_socket_event_callback(void *ctx) {
    simcoupler_ctx_t * sc_ctx = ctx;
    
    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = {0};
    int numberOfSocketsWithEvents = 0;

    size_t bufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
        &sc_ctx->socket_px4_receive,
        eventBuffer,
        bufferSize,
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

        if ((err = SharedResourceMutex_lock())) {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }

        bool cond = event.socketHandle >= 0 && event.socketHandle < OS_NETWORK_MAXIMUM_SOCKET_NO;
        if (!cond) {
            Debug_LOG_ERROR("Found invalid socket handle %d for event %d", event.socketHandle, i);
            goto reset_PX4;
        }


        uint8_t eventMask = event.eventMask;

        if (eventMask & OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN) {
            Debug_LOG_ERROR("Received event: OS_SOCK_EV_ERROR || eventMask & OS_SOCK_EV_FIN");
            err = OS_Socket_close(sc_ctx->handle_px4_receive);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
            sc_ctx->conn_px4_init = false;
            return;
        }

        if (eventMask & OS_SOCK_EV_CONN_ACPT) {
            Debug_LOG_ERROR("Conn accpt event");
            err = OS_Socket_accept(sc_ctx->handle_px4_receive, &sc_ctx->client_handle_px4, &sc_ctx->addr_px4_linux);
            if (err == OS_ERROR_TRY_AGAIN) {
                Debug_LOG_ERROR("Socket accept failed OS_ERROR_TRY_AGAIN");
                
                goto reset_PX4;
            } else if (err) {
                Debug_LOG_ERROR("OS_Socket_accept() failed, error %d", err);
                OS_Socket_close(sc_ctx->client_handle_px4);
                goto reset_PX4;
            }
            sc_ctx->conn_px4_init = true;
        } else if (eventMask & OS_SOCK_EV_READ) {
            static char buf[1500];
            size_t len_actual;
            size_t len_requested = sizeof(buf);

            err = OS_Socket_read(
                sc_ctx->client_handle_px4,
                buf, 
                len_requested,
                &len_actual);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_read() failed, code %d", err);
                goto reset_PX4;
            }

            // Check if the connection to the vm is established
            if (!sc_ctx->conn_vm_init) {
                Debug_LOG_ERROR("Connection to the vm is not initiated, data will be dropped");
                goto reset_PX4;
            }

            err = OS_Socket_write(sc_ctx->client_handle_vm, buf, len_actual, &len_actual);
            if (err) {
                Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
            }
        }
        
reset_PX4:
        memset(&eventBuffer[sc_ctx->handle_px4_receive.handleID], 0, sizeof(OS_Socket_Evt_t));

        err = SharedResourceMutex_unlock();
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }
    
    Debug_ASSERT(NULL != sc_ctx);
    err = OS_Socket_regCallback(
        &sc_ctx->socket_px4_receive,
        &socket_px4_socket_event_callback,
        ctx
    );
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}


void socket_vm_socket_event_callback(void *ctx) {
    Debug_ASSERT(NULL != ctx);
    simcoupler_ctx_t * sc_ctx = ctx;
    
    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = { 0 };
    int numberOfSocketsWithEvents = 0;
    size_t eventBufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
        &sc_ctx->socket_vm_send,
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

        if ((err = SharedResourceMutex_lock()))
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
            err = OS_Socket_close(
                    sc_ctx->handle_vm_send.handleID == event.socketHandle ? sc_ctx->handle_vm_send : 
                    sc_ctx->client_handle_vm);
                if (err) {
                    Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
                }
            sc_ctx->conn_vm_init = false;
        }

        if (eventMask & OS_SOCK_EV_CONN_ACPT) {
            Debug_LOG_ERROR("Conn accpt event");
            err = OS_Socket_accept(sc_ctx->handle_vm_send, &sc_ctx->client_handle_vm, &sc_ctx->addr_vm_linux);
            if (err == OS_ERROR_TRY_AGAIN) {
                Debug_LOG_ERROR("Socket accept failed OS_ERROR_TRY_AGAIN");
                goto reset_VM;
            } else if (err) {
                Debug_LOG_ERROR("OS_Socket_accept() failed, error %d", err);
                err = OS_Socket_close(
                    sc_ctx->handle_vm_send.handleID == event.socketHandle ? sc_ctx->handle_vm_send : 
                    sc_ctx->client_handle_vm);
                if (err) {
                    Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
                }
                goto reset_VM;
            }
            
            sc_ctx->conn_vm_init = true;
        }

reset_VM:
        memset(&eventBuffer[event.socketHandle], 0, sizeof(OS_Socket_Evt_t));
        
        if ((err = SharedResourceMutex_unlock())) {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    err = OS_Socket_regCallback(
        &sc_ctx->socket_vm_send,
        &socket_vm_socket_event_callback,
        (void*) &simcoupler_ctx);
    if (err)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}



//----------------------------------------------------------------------
// Init
//----------------------------------------------------------------------

void post_init(void) {
    OS_Error_t err;

    //Wait for the Networkstack to be initialized
    err = wait_for_nw_stack_init(&simcoupler_ctx.socket_px4_receive);
    if (err) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return;
    }

    err = wait_for_nw_stack_init(&simcoupler_ctx.socket_vm_send);
    if (err) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return;
    }

    //create sockets
    err = OS_Socket_create(
        &simcoupler_ctx.socket_px4_receive,
        &simcoupler_ctx.handle_px4_receive,
        OS_AF_INET,
        OS_SOCK_STREAM
    );
    if (err) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return;
    }

    err = OS_Socket_create(
        &simcoupler_ctx.socket_vm_send,
        &simcoupler_ctx.handle_vm_send,
        OS_AF_INET,
        OS_SOCK_STREAM
    );
    if (err) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return;
    }

    //register socket callback
    err = OS_Socket_regCallback(
              &simcoupler_ctx.socket_px4_receive,
              &socket_px4_socket_event_callback,
              (void*) &simcoupler_ctx);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }

    //register socket callback
    err = OS_Socket_regCallback(
              &simcoupler_ctx.socket_vm_send,
              &socket_vm_socket_event_callback,
              (void*) &simcoupler_ctx);
    if (err)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }

    //bind sockets to the address
    err = OS_Socket_bind(simcoupler_ctx.handle_px4_receive, &simcoupler_ctx.addr_px4_trentos);
    if (err) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(simcoupler_ctx.handle_px4_receive);
        if (err)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }

    err = OS_Socket_bind(simcoupler_ctx.handle_vm_send, &simcoupler_ctx.addr_vm_trentos);
    if (err) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(simcoupler_ctx.handle_vm_send);
        if (err)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }

    int backlog = 10;
    err = OS_Socket_listen(simcoupler_ctx.handle_px4_receive, backlog);
    if (err) {
        Debug_LOG_ERROR("OS_Socket_listen() failed, code %d", err);
        OS_Socket_close(simcoupler_ctx.handle_px4_receive);
    }

    err = OS_Socket_listen(simcoupler_ctx.handle_vm_send, backlog);
    if (err) {
        Debug_LOG_ERROR("OS_Socket_listen() failed, code %d", err);
        OS_Socket_close(simcoupler_ctx.handle_vm_send);
    }

    Debug_LOG_ERROR("Both network stacks are initialized.");
}
