/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */
#include <camkes.h>

#include "lib_debug/Debug.h"

#include "socket_helper.h"


OS_Error_t wait_for_nw_stack_init_nb(const if_OS_Socket_t * const nw_sock) {
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


OS_Error_t init_socket_nb(socket_ctx_t * ctx) {
    OS_Error_t err;

    //Wait for the Networkstack to be initialized
    if ((err = wait_for_nw_stack_init_nb(&ctx->socket))) {
        Debug_LOG_ERROR("NetworkStack experienced a fatal error: %d", err);
        return err;
    }

    
    //create sockets
    if ((err = OS_Socket_create(&ctx->socket, &ctx->handle, OS_AF_INET, OS_SOCK_STREAM))) {
        Debug_LOG_ERROR("OS_Socket_create() failed, code %d", err);
        return err;
    }

    
    //register socket callback
    if ((err = OS_Socket_regCallback(
              &ctx->socket,
              ctx->callback,
              ctx->callback_ctx))) {
        Debug_LOG_ERROR("OS_Socket_regCallback() failed, code %d", err);
    }
    return err;
}


OS_Error_t init_socket_nb_server(socket_ctx_t * ctx, int backlog) {
    OS_Error_t err;
    if ((err = init_socket_nb(ctx))) {
        return err;
    }

    //bind the socket to the address
    if ((err = OS_Socket_bind(ctx->handle, 
                              &ctx->addr))) {
        Debug_LOG_ERROR("OS_Socket_bind() failed, code %d", err);

        if ((err = OS_Socket_close(ctx->handle)))
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
        return err;
    }

    //listen for incoming connections
    if ((err = OS_Socket_listen(ctx->handle, backlog))) {
        Debug_LOG_ERROR("OS_Socket_listen() failed, code %d", err);
        if ((err = OS_Socket_close(ctx->handle)))
        {
            Debug_LOG_ERROR("OS_Socket_close() failed, code %d", err);
        }
    }
    return err;
}

OS_Error_t init_socket_nb_client(socket_ctx_t * ctx) {
    OS_Error_t err;
    if ((err = init_socket_nb(ctx))) {
        return err;
    }

    if ((err = OS_Socket_connect(ctx->handle, &(ctx->addr_partner)))) {
        Debug_LOG_ERROR("OS_Socket_connect() failed, code %d", err);
    }
    return err;
}