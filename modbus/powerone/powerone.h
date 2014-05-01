#ifndef _MB_POWERONE_H
#define _MB_POWERONE_H

#ifdef __cplusplus
PR_BEGIN_EXTERN_C
#endif

eMBErrorCode    ePoweroneInit( UCHAR slaveAddress, UCHAR ucPort, ULONG ulBaudRate,
                             UCHAR ucData, eMBParity eParity, UCHAR ucStop );
void            ePoweroneStart( void );
void            ePoweroneStop( void );
eMBErrorCode    ePoweroneReceive( UCHAR * pucRcvAddress, UCHAR ** pucFrame, USHORT * pusLength );
eMBErrorCode    ePoweroneSend( UCHAR slaveAddress, const UCHAR * pucFrame, USHORT usLength );
BOOL            xPoweroneReceiveFSM( void );
BOOL            xPoweroneTransmitFSM( void );
BOOL            xPoweroneTimerT15Expired( void );
BOOL            xPoweroneTimerT35Expired( void );

eMBErrorCode ePoweroneReadRegisters(UCHAR ucSlaveAddress, USHORT usRegStartAddress, 
                        UCHAR ubNRegs, UCHAR **pucRcvFrame, USHORT *pusLength);
eMBErrorCode ePoweroneSendData(UCHAR *data, USHORT len, UCHAR **pucRcvFrame, USHORT *pusLength);

#ifdef __cplusplus
PR_END_EXTERN_C
#endif
#endif
