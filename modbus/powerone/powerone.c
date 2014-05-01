/* ----------------------- System includes ----------------------------------*/
#include "stdlib.h"
#include "string.h"

/* ----------------------- Platform includes --------------------------------*/
#include "port.h"

/* ----------------------- Modbus includes ----------------------------------*/
#include "mb.h"
#include "powerone.h"
#include "mbframe.h"
#include "crc_x25.h"
#include "mbport.h"

/* ----------------------- Type definitions ---------------------------------*/
typedef enum
{
    STATE_RX_IDLE,              /*!< Receiver is in idle state. */
    STATE_RX_RCV,               /*!< Frame is beeing received. */
    STATE_RX_WAIT_EOF           /*!< Wait for End of Frame. */
} eMBRcvState;

typedef enum
{
    STATE_TX_IDLE,              /*!< Transmitter is in idle state. */
    STATE_TX_XMIT               /*!< Transmitter is in transfer state. */
} eMBSndState;

/* ----------------------- Static variables ---------------------------------*/
static volatile eMBSndState eSndState;
static volatile eMBRcvState eRcvState;

extern volatile UCHAR ucMBBuf[EXTRA_HEAD_ROOM + MB_SER_PDU_SIZE_MAX];

static volatile UCHAR *pucSndBufferCur;
static volatile USHORT usSndBufferCount;
static volatile UCHAR ucSendBuf[10];

static volatile USHORT usRcvBufferPos;
static volatile UCHAR ucRcvBuf[8];

typedef struct {
	UCHAR inst;
	UCHAR type;
} instMap_t;

static instMap_t instmap[] = {
/*0		0-2*/	{63, 	0},
/*1		3-5*/	{50,	0},
/*2		6-8*/	{78,	0},
/*3		9-11*/	{78,	1},
/*4		12-14*/	{78,	2},
/*5		15-17*/	{78,	3},
/*6		18-20*/	{78,	4},
/*7		21-23*/	{78,	5},
/*8		24-26*/	{78,	6},
/*9		27-29*/	{59,	1},
/*10	30-32*/	{59,	2},
/*11	33-35*/	{59,	3},
/*12	36-38*/	{59,	4},
/*13	39-41*/	{59,	21},
/*14	42-44*/	{59,	22},
/*15	45-47*/	{59,	23},
/*16	48-50*/	{59,	25},
/*17	51-53*/	{59,	26},
/*18	54-56*/	{59,	27},
/*19	57-59*/	{59,	28},
/*20	60-62*/	{59,	29},
/*21	63-65*/	{59,	34},
/*22	66-68*/	{59,	35},
/*23	69-71*/	{59,	39},
/*24	72-74*/	{59,	40},
/*25	75-77*/	{59,	41},
/*26	78-80*/	{59,	42},
/*27	81-83*/	{59,	43},
/*28	84-86*/	{59,	44},
/*29	87-89*/	{59,	61},
/*30	90-92*/	{59,	62},
/*31	93-95*/	{59,	63}
};

/* ----------------------- Start implementation -----------------------------*/
eMBErrorCode
ePoweroneInit( UCHAR ucSlaveAddress, UCHAR ucPort, ULONG ulBaudRate, UCHAR ucData, eMBParity eParity, UCHAR ucStop )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    ULONG           usTimerT35_50us;

    ( void )ucSlaveAddress;
    ENTER_CRITICAL_SECTION(  );

    if( xMBPortSerialInit( ucPort, ulBaudRate, ucData, eParity, ucStop) != TRUE )
    {
        eStatus = MB_EPORTERR;
    }
    else
    {
        /* If baudrate > 19200 then we should use the fixed timer values
         * t35 = 1750us. Otherwise t35 must be 3.5 times the character time.
         */
        if( ulBaudRate > 19200 )
        {
            usTimerT35_50us = 35;       /* 1800us. */
        }
        else
        {
            /* The timer reload value for a character is given by:
             *
             * ChTimeValue = Ticks_per_1s / ( Baudrate / 11 )
             *             = 11 * Ticks_per_1s / Baudrate
             *             = 220000 / Baudrate
             * The reload for t3.5 is 1.5 times this value and similary
             * for t3.5.
             */
            usTimerT35_50us = ( 7UL * 220000UL ) / ( 2UL * ulBaudRate );
        }
        if( xMBPortTimersInit( ( USHORT ) usTimerT35_50us ) != TRUE )
        {
            eStatus = MB_EPORTERR;
        }
    }
    EXIT_CRITICAL_SECTION(  );

    return eStatus;
}

void
ePoweroneStart( void )
{
    ENTER_CRITICAL_SECTION(  );
	eRcvState = STATE_RX_IDLE;
    vMBPortSerialEnable( FALSE, FALSE );
	vMBPortTimersDisable(  );
    EXIT_CRITICAL_SECTION(  );
}

void
ePoweroneStop( void )
{
    ENTER_CRITICAL_SECTION(  );
    vMBPortSerialEnable( FALSE, FALSE );
    vMBPortTimersDisable(  );
    EXIT_CRITICAL_SECTION(  );
}

static UCHAR ucSlaveAddr;
static USHORT usRegStart;
static USHORT usRegEnd;

eMBErrorCode
ePoweroneNextCmd(UCHAR **pucCmd)
{
	USHORT usRegGrp;

	if (usRegStart > usRegEnd)
		return MB_ENOREG;

	usRegGrp = usRegStart / 3;
	pucSndBufferCur = *pucCmd;

	pucSndBufferCur[0] = ucSlaveAddr;
	pucSndBufferCur[1] = instmap[usRegGrp].inst;
	pucSndBufferCur[2] = instmap[usRegGrp].type;
	pucSndBufferCur[3] = 0;
	pucSndBufferCur[4] = 0;
	pucSndBufferCur[5] = 0;
	pucSndBufferCur[6] = 0;
	pucSndBufferCur[7] = 0;

	usRegStart += 3;
	return MB_ENOERR;
}

eMBErrorCode ePoweroneReadRegisters(UCHAR ucSlaveAddress, USHORT usRegStartAddress, 
                        UCHAR ubNRegs, UCHAR **pucRcvFrame, USHORT *pusLength) 
{
    eMBErrorCode eStatus;
    eMBEventType eEvent;
	UCHAR *ucRequest = ( UCHAR * )ucSendBuf;
	UCHAR *ucAnswer;
	*pucRcvFrame = ( UCHAR * )(ucMBBuf + EXTRA_HEAD_ROOM);
	UCHAR *ucRcvFrame = *pucRcvFrame;
	int i = 3;

	ucSlaveAddr = ucSlaveAddress;
	usRegStart = usRegStartAddress;
	usRegEnd = usRegStart + ubNRegs - 1;

	while (ePoweroneNextCmd(&ucRequest) != MB_ENOREG) {
		eStatus = ePoweroneSend( 0, ucRequest, 8);
		if (eStatus == MB_ENOERR) {
			/* wait on receive event */
		    if( xMBPortEventGet( &eEvent ) == TRUE ) {  
				eStatus = ePoweroneReceive( NULL, &ucAnswer, NULL );
		    }
			else {
				return MB_ETIMEDOUT;
			}
		}

		if (eStatus != MB_ENOERR)
			return eStatus;

		memcpy(ucRcvFrame + i, ucAnswer, 6);
		i += 6;
	}

	ucRcvFrame[0] = ucSlaveAddress;
	ucRcvFrame[1] = 0x03;
	ucRcvFrame[2] = i - 3;
	*pusLength = i;
	return MB_ENOERR;
}

eMBErrorCode ePoweroneSendData(UCHAR *data, USHORT len, UCHAR **pucRcvFrame, USHORT *pusLength) 
{
	USHORT numRegs;
	ucSlaveAddr = data[0];
	usRegStart = ((USHORT)data[2] << 8) | (USHORT)data[3];
	numRegs = ((USHORT)data[4] << 8) | (USHORT)data[5];
	usRegEnd = usRegStart + numRegs - 1;
	
	if ((usRegStart > 93) || (usRegStart % 3) || (numRegs % 3))
		return MB_ENOREG;

	return ePoweroneReadRegisters(ucSlaveAddr, usRegStart, numRegs, pucRcvFrame, pusLength);
}

eMBErrorCode
ePoweroneReceive( UCHAR * pucRcvAddress, UCHAR ** pucFrame, USHORT * pusLength )
{
    BOOL            xFrameReceived = FALSE;
    eMBErrorCode    eStatus = MB_ENOERR;
    USHORT	usCRC16;

    ENTER_CRITICAL_SECTION(  );
    assert( usRcvBufferPos == 8 );

    /* CRC check */
    usCRC16 = Calc_CRC( ( UCHAR * ) ucRcvBuf, 6 );
    if( (ucRcvBuf[6] == ( UCHAR )( usCRC16 & 0xFF )) &&
    		(ucRcvBuf[7] == ( UCHAR )( usCRC16 >> 8 )))
    {
        /* Return the start of the PDU to the caller. */
        *pucFrame = ( UCHAR * ) & ucRcvBuf[0];
        xFrameReceived = TRUE;
    }
    else
    {
        eStatus = MB_EIO;
    }

    EXIT_CRITICAL_SECTION(  );

    return eStatus;
}

eMBErrorCode
ePoweroneSend( UCHAR ucSlaveAddress, const UCHAR * pucFrame, USHORT usLength )
{
    eMBErrorCode    eStatus = MB_ENOERR;
    USHORT          usCRC16;
	
    ENTER_CRITICAL_SECTION(  );

    /* Check if the receiver is still in idle state. If not we where to
     * slow with processing the received frame and the master sent another
     * frame on the network. We have to abort sending the frame.
     */
    if( eRcvState == STATE_RX_IDLE )
    {
        pucSndBufferCur = ( UCHAR * ) pucFrame;
		
        /* Calculate CRC16 checksum for Serial-Line-PDU. */
        usCRC16 = Calc_CRC( ( UCHAR * ) pucSndBufferCur, 8 );
        pucSndBufferCur[8] = ( UCHAR )( usCRC16 & 0xFF );
        pucSndBufferCur[9] = ( UCHAR )( usCRC16 >> 8 );
		usSndBufferCount = 10;

        /* Activate the transmitter. */
        eSndState = STATE_TX_XMIT;
        vMBPortSerialEnable( FALSE, TRUE );
    }
    else
    {
        eStatus = MB_EIO;
    }
    EXIT_CRITICAL_SECTION(  );

	vTaskSuspendAll();
	while (!pxMBFrameCBTransmitterEmpty());
	xTaskResumeAll();

    return eStatus;
}

BOOL
xPoweroneReceiveFSM( void )
{
    BOOL            xNeedPoll = FALSE;
    UCHAR           ucByte;

    assert( eSndState == STATE_TX_IDLE );

    ( void )xMBPortSerialGetByte( ( CHAR * ) & ucByte );
    switch ( eRcvState )
    {
    case STATE_RX_RCV:
        ucRcvBuf[usRcvBufferPos++] = ucByte;
		if( usRcvBufferPos == 7 )
			eRcvState = STATE_RX_WAIT_EOF;
		vMBPortTimersEnable(  );
        break;

    case STATE_RX_WAIT_EOF:
        /* Disable  timeout timer because all bytes are received. */
        vMBPortTimersDisable(  );
		ucRcvBuf[usRcvBufferPos++] = ucByte;
        /* Receiver is again in idle state. */
        eRcvState = STATE_RX_IDLE;

        /* Notify the caller of ePoweroneReceive that a new frame was received. */
        xNeedPoll = xMBPortEventPost( EV_FRAME_RECEIVED );
        vMBPortSerialEnable( FALSE, FALSE );
        break;

    case STATE_RX_IDLE:
        usRcvBufferPos = 0;
        ucRcvBuf[usRcvBufferPos++] = ucByte;
        eRcvState = STATE_RX_RCV;
        vMBPortTimersEnable(  );
        break;
    }

    return xNeedPoll;
}


BOOL
xPoweroneTransmitFSM( void )
{
    BOOL            xNeedPoll = FALSE;

    assert( eRcvState == STATE_RX_IDLE );

    switch ( eSndState )
    {
        /* We should not get a transmitter event if the transmitter is in
         * idle state.  */
    case STATE_TX_IDLE:
        /* enable receiver/disable transmitter. */
        vMBPortSerialEnable( TRUE, FALSE );
        xNeedPoll = TRUE;
        break;

    case STATE_TX_XMIT:
        /* check if we are finished. */
        if( usSndBufferCount != 0 )
        {
            xMBPortSerialPutByte( ( CHAR )*pucSndBufferCur );
            pucSndBufferCur++;  /* next byte in sendbuffer. */
            usSndBufferCount--;
        }
        else
        {
            eSndState = STATE_TX_IDLE;
        }
        break;
    }

    return xNeedPoll;
}

BOOL
xPoweroneTimerT35Expired( void )
{
    switch ( eRcvState )
    {
        /* If we have a timeout we go back to the idle state and wait for
         * the next frame.
         */
    case STATE_RX_RCV:
    case STATE_RX_WAIT_EOF:
        eRcvState = STATE_RX_IDLE;
        break;

    default:
        assert( ( eRcvState == STATE_RX_RCV ) || ( eRcvState == STATE_RX_WAIT_EOF ) );
        break;
    }
    vMBPortTimersDisable(  );

    /* no context switch required. */
    return FALSE;
}
