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

#include "libs/mavlink_filter/mavlink_filter.h"

#include <camkes.h>

//----------------------------------------------------------------------
// Network
//----------------------------------------------------------------------

typedef void (*callbackFunc_t)(void*);


typedef struct {
    const if_OS_Socket_t    socket;
    const OS_Socket_Addr_t  addr;
    OS_Socket_Addr_t        addr_partner;
    bool                    addr_set;
    const bool              filter;
    callbackFunc_t          callback; 
    OS_Socket_Handle_t      handle;
} socket_ctx_t;


void socket_GCS_event_callback(void* ctx);
void socket_PX4_event_callback(void* ctx);


// VM       <--> TRENTOS
socket_ctx_t socket_GCS = {
    .socket = IF_OS_SOCKET_ASSIGN(socket_GCS_nws),
    .addr = {
        .addr = GCS_TRENTOS_ADDR,
        .port = GCS_TRENTOS_PORT
    },
    .addr_partner = {
        .addr = GCS_VM_ADDR,
        .port = 0           //not defined as this port is dynamic
        },
    .callback = socket_GCS_event_callback,
    .addr_set = false,
    .filter = true
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
    .addr_set = true,
    .addr_set   = true,
    .filter     = false
};


void
socket_event_callback(
    socket_ctx_t * socket_from,
    socket_ctx_t * socket_to)
{
    if (!socket_to->addr_set) {
        Debug_LOG_ERROR("Connection has to be established by GCS");
        return;
    }

    OS_Socket_Evt_t eventBuffer[OS_NETWORK_MAXIMUM_SOCKET_NO] = {0};
    int numberOfSocketsWithEvents = 0;

    size_t bufferSize = sizeof(eventBuffer);

    OS_Error_t err = OS_Socket_getPendingEvents(
                         &socket_from->socket,
                         eventBuffer,
                         bufferSize,
                         &numberOfSocketsWithEvents);    
    ASSERT_EQ_OS_ERR(OS_SUCCESS, err);

    // Verify that the received number of sockets with events is within expected
    // bounds.
    ASSERT_LE_INT(numberOfSocketsWithEvents, OS_NETWORK_MAXIMUM_SOCKET_NO);
    ASSERT_GT_INT(numberOfSocketsWithEvents, -1);

    for (int i = 0; i < numberOfSocketsWithEvents; i++)
    {
        char buf[4096] = {0};
        size_t len_requested = sizeof(buf);
        size_t len_actual = 0;
        OS_Socket_Addr_t srcAddr = {0};

        err = SharedResourceMutex_lock();
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("Mutex lock failed, code %d", err);
            return;
        }

        err = OS_Socket_recvfrom(socket_from->handle, buf, len_requested, &len_actual, &srcAddr);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_TRACE("OS_Socket_recvfrom() failed, code %d", err);
            goto reset;
        }

        //GCS address is not fixed therfore we set it here
        // -> it is expected that the GCS starts the conversation
        if (!socket_from->addr_set) {
            socket_from->addr_set = true;
            socket_from->addr_partner.port = srcAddr.port;
            printf("Set GCS IP address to: IP: %s PORT: %d\n", srcAddr.addr, ntohs(srcAddr.port));
        }

        if (socket_from->filter) {
            //Applying filter to data from GCS -> PX4
            if (filter_mavlink_message(buf, len_actual)) {
                Debug_LOG_ERROR("Packet dropped: violation of filter rules");
            }
        }

        err = OS_Socket_sendto(socket_to->handle, buf, len_actual, &len_actual, &socket_to->addr_partner); 
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
            err = OS_Socket_close(socket_from->handle);
            if (err != OS_SUCCESS)
            {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
        }

reset:
        memset(&eventBuffer[socket_to->handle.handleID], 0, sizeof(OS_Socket_Evt_t));

        err = SharedResourceMutex_unlock();
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("Mutex unlock failed, code %d", err);
            return;
        }
    }

    err = OS_Socket_regCallback(
        &socket_from->socket,
        socket_from->callback,
        (void*) socket_from
    );
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
    ASSERT_EQ_OS_ERR(OS_SUCCESS, err);
}


void socket_GCS_event_callback(void* ctx)
{
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = ctx; 
    Debug_ASSERT(socket_from != &socket_PX4);
    socket_ctx_t * socket_to = &socket_PX4;
    socket_event_callback(socket_from, socket_to);
}


void socket_PX4_event_callback(void* ctx) 
{
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = ctx;
    Debug_ASSERT(socket_from != &socket_GCS);
    socket_ctx_t * socket_to = &socket_GCS;
    socket_event_callback(socket_from, socket_to);
}

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


void socket_init(socket_ctx_t *socket_ctx) {
    OS_Error_t err;

    //Wait for the Networkstack to be initialized
    err = wait_for_nw_stack_init(&(socket_ctx->socket));
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return;
    }

    //create socket  
    err = OS_Socket_create(
        &(socket_ctx->socket),
        &(socket_ctx->handle),
        OS_AF_INET,
        OS_SOCK_DGRAM
    );
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return;
    }

    //register socket callback
    err = OS_Socket_regCallback(
              &(socket_ctx->socket),
              socket_ctx->callback,
              (void*) socket_ctx);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }

    //bind the socket to the address
    err = OS_Socket_bind(socket_ctx->handle, &(socket_ctx->addr));
    if (err != OS_SUCCESS) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        err = OS_Socket_close(socket_ctx->handle);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }
}


void post_init(void)
{
    socket_init(&socket_GCS);
    socket_init(&socket_PX4);
}
