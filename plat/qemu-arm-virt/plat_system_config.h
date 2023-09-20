/*
 *  Platform defaults for the QEMU Arm virt.
 *
 *  Copyright (C) 2023, HENSOLDT Cyber GmbH
 *
*/


// kernel log uses UART_0, so we can use UART_1 for i/o test
//#define UART_CHANMUX UART_1 Currently not implemented
#define UART_IO      UART_2
