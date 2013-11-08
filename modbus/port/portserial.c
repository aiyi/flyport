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
 * File: $Id: portserial.c,v 1.1 2006/08/22 21:35:13 wolti Exp $
 */

#include "port.h"
#include "RS485Helper.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "mbport.h"

#define		UART_STOP_BITS		0
#define		UART_DATA_PARITY	1

#define		UART_ONE_STOP 		0
#define		UART_TWO_STOP		1

#define		UART_8BITS_PARITY_EVEN 4
#define		UART_8BITS_PARITY_ODD  2
#define		UART_8BITS_PARITY_NONE	0
#define		UART_9BITS_PARITY_NONE	6

/* ----------------------- Start implementation -----------------------------*/
void
vMBPortSerialEnable( BOOL xRxEnable, BOOL xTxEnable )
{
    /* If xRXEnable enable serial receive interrupts. If xTxENable enable
     * transmitter empty interrupts.
     */
	if(xRxEnable){
		EnableIntU2RX;
	}
	else {
		DisableIntU2RX;
	}
	
	if(xTxEnable){
		RS485TxEnable(2);
		EnableIntU2TX;
		IFS1bits.U2TXIF = 1;
	}
	else {
		while(BusyUART2());
		RS485TxDisable(2);
		DisableIntU2TX;
	}
}

BOOL
xMBPortSerialInit( UCHAR ucPORT, ULONG ulBaudRate, UCHAR ucDataBits, eMBParity eParity )
{
	int dataParity;
	int stopBits;

	switch (eParity)
	{
	case MB_PAR_NONE:
		dataParity = UART_8BITS_PARITY_NONE;
		stopBits = UART_TWO_STOP;
		break;
	case MB_PAR_ODD:
		dataParity = UART_8BITS_PARITY_ODD;
		stopBits = UART_ONE_STOP;
		break;
	case MB_PAR_EVEN:
		dataParity = UART_8BITS_PARITY_EVEN;
		stopBits = UART_ONE_STOP;
		break;
	default:
		return FALSE;
	}

	if (ucDataBits == 7)
	{
		dataParity = UART_8BITS_PARITY_NONE;
		stopBits = UART_ONE_STOP;
	}

	// Initialize the RS485
	RS485Off(ucPORT);
	RS485Init(ucPORT, ulBaudRate);
	RS485SetParam(ucPORT, RS485_STOP_BITS, stopBits);
	RS485SetParam(ucPORT, RS485_DATA_PARITY, dataParity);
	RS485On(ucPORT);
    return TRUE;
}

BOOL
xMBPortSerialPutByte( CHAR ucByte )
{
    /* Put a byte in the UARTs transmit buffer. This function is called
     * by the protocol stack if pxMBFrameCBTransmitterEmpty( ) has been
     * called. */
	while(BusyUART2());
	WriteUART2((unsigned int)ucByte);
    return TRUE;
}

BOOL
xMBPortSerialGetByte( CHAR * pucByte )
{
    /* Return the byte in the UARTs receive buffer. This function is called
     * by the protocol stack after pxMBFrameCBByteReceived( ) has been called.
     */
	while(!DataRdyUART2());
	*pucByte = (CHAR)ReadUART2();
    return TRUE;
}

/* Create an interrupt handler for the transmit buffer empty interrupt
 * (or an equivalent) for your target processor. This function should then
 * call pxMBFrameCBTransmitterEmpty( ) which tells the protocol stack that
 * a new character can be sent. The protocol stack will then call 
 * xMBPortSerialPutByte( ) to send the character.
 */
void __attribute__((interrupt, no_auto_psv)) _U2TXInterrupt(void)
{
	U2TX_Clear_Intr_Status_Bit;
    pxMBFrameCBTransmitterEmpty(  );
}

/* Create an interrupt handler for the receive interrupt for your target
 * processor. This function should then call pxMBFrameCBByteReceived( ). The
 * protocol stack will then call xMBPortSerialGetByte( ) to retrieve the
 * character.
 */
void __attribute__((interrupt, no_auto_psv)) _U2RXInterrupt(void)
{
	U2RX_Clear_Intr_Status_Bit;
    pxMBFrameCBByteReceived(  );
}
