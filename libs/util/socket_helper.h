/*
 * Copyright (C) 2023, HENSOLDT Cyber GmbH
 */

#pragma once

#include "OS_Socket.h"
#include "interfaces/if_OS_Socket.h"

typedef void (*callbackFunc_t)(void*);

typedef struct {
    const if_OS_Socket_t        socket;
    OS_Socket_Handle_t          handle;
    OS_Socket_Handle_t          client_handle;
    const OS_Socket_Addr_t      addr;
    OS_Socket_Addr_t      addr_partner;
    callbackFunc_t              callback;
    void *                      callback_ctx;
    bool                        conn_init;
} socket_ctx_t;


//Because we do try to connect to us self, because we use the addr partner and addr differently when we connect client / server.

OS_Error_t init_socket_nb_server(socket_ctx_t *, int);

OS_Error_t init_socket_nb_client(socket_ctx_t *);