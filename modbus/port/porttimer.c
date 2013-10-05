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
 * File: $Id: porttimer.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

/* ----------------------- Static variables ---------------------------------*/

static unsigned int period = 0;

/* ----------------------- Start implementation -----------------------------*/
BOOL
xMBPortTimersInit( USHORT usTim1Timerout50us )
{
	period = ( 62500 * usTim1Timerout50us ) / ( 20000 );
    return TRUE;
}

inline void
vMBPortTimersEnable(  )
{
    /* Enable the timer with the timeout passed to xMBPortTimersInit( ) */
	T4CONbits.TCKPS = 3; //clock divider=256
	PR4 = period; //limit to raise interrupt=62500
	TMR4 = 0; // init timer counter value
	// interrupt config
	IFS1bits.T4IF = 0; //interrupt flag off
	IEC1bits.T4IE = 1; //interrupt activated
	T4CONbits.TON = 1; // timer start
}

inline void
vMBPortTimersDisable(  )
{
    /* Disable any pending timers. */
	T4CON = 0;  //turn off timer
}

/* Create an ISR which is called whenever the timer has expired. This function
 * must then call pxMBPortCBTimerExpired( ) to notify the protocol stack that
 * the timer has expired.
 */
void __attribute__ ((interrupt,no_auto_psv)) _T4Interrupt (void)
{
	IFS1bits.T4IF = 0;
	pxMBPortCBTimerExpired(  );
}

