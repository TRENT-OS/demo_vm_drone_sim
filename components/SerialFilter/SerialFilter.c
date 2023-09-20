/*
 * Copyright (C) 2021-2023, HENSOLDT Cyber GmbH
 */
 
 
#include "lib_debug/Debug.h"
#include <string.h>

#include "OS_Socket.h"
#include "interfaces/if_OS_Socket.h"

#include "OS_Dataport.h"
#include "lib_io/FifoDataport.h"

#include "lib_macros/Check.h"
#include "lib_macros/Test.h"
#include <arpa/inet.h>

#include "libs/mavlink_filter/mavlink_filter.h"

#include <camkes.h>

#include "common/mavlink.h"
#include "mavlink_helpers.h"

/* 1500 is the standard ethernet MTU at the network layer. */
#define ETHER_MTU 1500


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

typedef struct {
    FifoDataport* uart_input_fifo;
    uint8_t* uart_output_fifo;
} uart_ctx_t;

// VM       <--> TRENTOS
void socket_GCS_event_callback(void* ctx);

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


mavlink_status_t mavlink_status;
mavlink_message_t mavlink_msg;
uint8_t mavlink_channel = MAVLINK_COMM_0;

// VM       <--> TRENTOS
void uart_PX4_event_callback(void* ctx);

uart_ctx_t uart_PX4 = { 0 };



void print_bytes(void *ptr, int size) 
{
    unsigned char *p = ptr;
    int i;
    for (i=0; i<size; i++) {
        printf("%02hhX ", p[i]);
    }
    printf("\n");
}


void uart_PX4_write(char* bytes, size_t amount) {
    uart_ctx_t *ctx = &uart_PX4;
    
    memcpy(ctx->uart_output_fifo, bytes, amount);
    
    uart_rpc_write(amount);
}


inline static void uart_PX4_pack_sent_msg(uint8_t byte) {
    //Debug_LOG_ERROR("Received byte, parsing: %c", byte);
    if (mavlink_parse_char(mavlink_channel, byte, &mavlink_msg, &mavlink_status)) {
        Debug_LOG_ERROR("Complete Mavlink message received");
        uint8_t buf[280] = { 0 };
        uint16_t length = mavlink_msg_to_send_buffer(buf, &mavlink_msg);

        if (!socket_GCS.addr_set) {
            Debug_LOG_ERROR("Connection Partner not initialized!: Dropping Packet");
            return;
        }

        size_t len = (size_t) length;
        int err = OS_Socket_sendto(socket_GCS.handle, buf, len, &len, &socket_GCS.addr_partner);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
            err = OS_Socket_close(socket_GCS.handle);
            if (err != OS_SUCCESS)
            {
                Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
            }
        }
    }
    /*
    size_t len = 1;
    int err = OS_Socket_sendto(socket_GCS.handle, &byte, len, &len, &socket_GCS.addr_partner);
    if (err != OS_SUCCESS)
    {
        Debug_LOG_ERROR("OS_Socket_sendto() failed, code %d", err);
        err = OS_Socket_close(socket_GCS.handle);
        if (err != OS_SUCCESS)
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }*/
}


int uart_PX4_read(uart_ctx_t *ctx) {
    FifoDataport* fifo = ctx->uart_input_fifo;
    void *buffer = NULL;
    size_t avail = FifoDataport_getContiguous(fifo, &buffer);

    for (int i = 0; i < avail; i++) {
        uint8_t *byte = buffer + i;
        uart_PX4_pack_sent_msg(*byte);        
    }

    FifoDataport_remove(fifo, avail);
    //printf("SerialFilter: 3\n");
    return 0;
}


void
socket_event_callback(
    socket_ctx_t * socket_from,
    uart_ctx_t * uart_to)
{
    //printf("Socket event callback\n");

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
        char buf[ETHER_MTU] = {0};
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

        //Debug_LOG_ERROR("\n\nRECEIVED MESSAGE FROM GCS!!!\n%s\n", buf);

        //Applying filter to data from GCS -> PX4
        if (filter_mavlink_message(buf, len_actual)) {
            Debug_LOG_ERROR("Packet dropped: violation of filter rules");
            goto reset;
        }
        
        uart_PX4_write(buf, len_actual);

reset:
        memset(&eventBuffer[socket_from->handle.handleID], 0, sizeof(OS_Socket_Evt_t));

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
    //Debug_LOG_ERROR("GCS event callback triggered!");
    Debug_ASSERT(NULL != ctx);
    socket_ctx_t * socket_from = ctx;
    socket_event_callback(socket_from, &uart_PX4);
}


void uart_PX4_event_callback(void* ctx) 
{
    Debug_ASSERT(NULL != ctx);
    //Debug_LOG_ERROR("UART callback triggered!");

    uart_PX4_read(ctx);

    int err = uart_event_reg_callback((void *) &uart_PX4_event_callback, (void *) ctx);
    if (err) {
        Debug_LOG_ERROR("uart_event_reg_callback() failed, code: %d", err);
    }
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


void uart_init(void) {
    uart_ctx_t *ctx = &uart_PX4;

    OS_Dataport_t input_port  = OS_DATAPORT_ASSIGN(uart_input_port);
    OS_Dataport_t output_port = OS_DATAPORT_ASSIGN(uart_output_port);

    ctx->uart_input_fifo  = (FifoDataport *) OS_Dataport_getBuf(input_port);
    ctx->uart_output_fifo = (uint8_t *) OS_Dataport_getBuf(output_port);
    
    int err = uart_event_reg_callback((void *) &uart_PX4_event_callback, (void *) ctx);
    if (err) {
        Debug_LOG_ERROR("uart_event_reg_callback() failed, code: %d", err);
    }
    Debug_LOG_ERROR("uart callback registered!");
}


void post_init(void)
{
    socket_init(&socket_GCS);
    uart_init();
}