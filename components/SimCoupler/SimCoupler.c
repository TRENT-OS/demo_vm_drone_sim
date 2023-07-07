/*
 * Copyright (C) 2021-2023, HENSOLDT Cyber GmbH
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
// Network
//----------------------------------------------------------------------

typedef struct {
    // Receive: PX4(Linux Host) -> Trentos(SimCoupler)
    const if_OS_Socket_t    socket_px4_receive;
    OS_Socket_Handle_t      handle_px4_receive;
    const OS_Socket_Addr_t  addr_px4_trentos;

    // Send: Trentos(SimCoupler) -> VM
    const if_OS_Socket_t    socket_gcs_send;
    OS_Socket_Handle_t      handle_gcs_send;
    const OS_Socket_Addr_t  addr_gcs_trentos;
    const OS_Socket_Addr_t  addr_gcs_vm;
} simcoupler_ctx_t;

void socket_px4_receive_event_callback(void* ctx);

simcoupler_ctx_t simcoupler_ctx = {
    .socket_px4_receive = IF_OS_SOCKET_ASSIGN(socket_PX4_nws),
    .addr_px4_trentos = {
        .addr = PX4_TRENTOS_ADDR,
        .port = PX4_TRENTOS_PORT_SIMCOUPLER
    },
    .socket_gcs_send = IF_OS_SOCKET_ASSIGN(socket_VM_nws),
    .addr_gcs_trentos = {
        .addr = VM_TRENTOS_ADDR,
        .port = VM_TRENTOS_PORT_SIMCOUPLER
    },
    .addr_gcs_vm = {
        .addr = VM_VM_ADDR,
        .port = VM_VM_PORT_SIMCOUPLER
    }
};

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

void socket_px4_receive_event_callback(void *ctx) {
    ZF_LOGE("Received Callback");
    OS_Error_t err;
    simcoupler_ctx_t * sc_ctx = ctx;
    
    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = {0};
    int numberOfSocketsWithEvents = 0;

    size_t bufferSize = sizeof(eventBuffer);

    err = OS_Socket_getPendingEvents(
        &sc_ctx->socket_px4_receive,
        eventBuffer,
        bufferSize,
        &numberOfSocketsWithEvents);
    ASSERT_EQ_OS_ERR(OS_SUCCESS, err);

    // Verify that the received number of sockets with events is within expected
    // bounds.
    ASSERT_LE_INT(numberOfSocketsWithEvents, OS_NETWORK_MAXIMUM_SOCKET_NO);
    ASSERT_GT_INT(numberOfSocketsWithEvents, -1);

    for (int i = 0; i < numberOfSocketsWithEvents; i++) {
        ZF_LOGE("HERE");
        char buf[4096] = {0};
        size_t len_requested = sizeof(buf);
        size_t len_actual = 0;
        OS_Socket_Addr_t srcAddr = {0};

        err = SharedResourceMutex_lock();
        if (err != OS_SUCCESS) {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }

        err = OS_Socket_recvfrom(sc_ctx->handle_px4_receive, buf, len_requested, &len_actual, &srcAddr);
        if (err != OS_SUCCESS) {
            Debug_LOG_TRACE("OS_Socket_recvfrom() failed, code %d", err);
            ZF_LOGE("RECVFROM FAILED");
            goto reset;
        }

        err = OS_Socket_sendto(sc_ctx->handle_gcs_send, buf, len_actual, &len_actual, &sc_ctx->addr_gcs_vm); 
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
            err = OS_Socket_close(sc_ctx->handle_px4_receive);
            if (err != OS_SUCCESS)
            {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
        }

reset:
        ZF_LOGE("MEMSET");
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
        &socket_px4_receive_event_callback,
        ctx
    );
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
}


void post_init(void) {
    OS_Error_t err;

    //Wait for the Networkstack to be initialized
    err = wait_for_nw_stack_init(&simcoupler_ctx.socket_px4_receive);
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return;
    }

    err = wait_for_nw_stack_init(&simcoupler_ctx.socket_gcs_send);
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return;
    }

    //create sockets
    err = OS_Socket_create(
        &simcoupler_ctx.socket_px4_receive,
        &simcoupler_ctx.handle_px4_receive,
        OS_AF_INET,
        OS_SOCK_DGRAM
    );
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return;
    }

    err = OS_Socket_create(
        &simcoupler_ctx.socket_gcs_send,
        &simcoupler_ctx.handle_gcs_send,
        OS_AF_INET,
        OS_SOCK_DGRAM
    );
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return;
    }

    //register socket callback
    err = OS_Socket_regCallback(
              &simcoupler_ctx.socket_px4_receive,
              &socket_px4_receive_event_callback,
              (void*) &simcoupler_ctx);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }

    //bind sockets to the address
    err = OS_Socket_bind(simcoupler_ctx.handle_px4_receive, &simcoupler_ctx.addr_px4_trentos);
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(simcoupler_ctx.handle_px4_receive);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }

    err = OS_Socket_bind(simcoupler_ctx.handle_gcs_send, &simcoupler_ctx.addr_gcs_trentos);
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(simcoupler_ctx.handle_gcs_send);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }

    ZF_LOGE("Both network stacks are initialized.");
}

