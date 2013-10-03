/*
 * FreeModbus Libary: BARE Port
 * Copyright (C) 2006 Christian Walter <wolti@sil.at>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * File: $Id: portevent.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

/* ----------------------- FreeRTOS -----------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

#ifndef MB_EVENT_GET_TIMEOUT_MS
#define MB_EVENT_GET_TIMEOUT_MS    ( 1000 )
#endif

/* ----------------------- Variables ----------------------------------------*/
static xQueueHandle xQueueHdl;


/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortEventInit( void )
{
    BOOL            bStatus = FALSE;
    if( 0 != ( xQueueHdl = xQueueCreate( 1, sizeof( eMBEventType ) ) ) )
    {
        bStatus = TRUE;
    }
    return bStatus;
}

BOOL
xMBPortEventPost( eMBEventType eEvent )
{
#ifdef MB_MASTER
    /* Master only care about EV_FRAME_RECEIVED event */
    if( eEvent == EV_FRAME_RECEIVED )
#endif
    {
        ( void )xQueueSendFromISR( xQueueHdl, ( const void * )&eEvent, pdFALSE );
    }

    return TRUE;
}

BOOL
xMBPortEventGet( eMBEventType * eEvent )
{
    BOOL            xEventHappened = FALSE;

    if( pdTRUE == xQueueReceive( xQueueHdl, eEvent, portTICK_RATE_MS * MB_EVENT_GET_TIMEOUT_MS ) )
    {
        xEventHappened = TRUE;
    }
    return xEventHappened;
}
