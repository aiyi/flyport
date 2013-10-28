#include "TCPClient.h"
#include "hilo.h"
#include "GSMData.h"

extern GSMModule mainGSM;
extern int mainGSMStateMachine;

void TCPClient_init(TCPClient_t *this)
{
	this->sock.number = INVALID_SOCKET;
	this->sock.notif = -1;
	this->sock.rxLen = 0;
	this->size = 0;
	this->idx = 0;
	this->tick = tickGetSeconds();
}

static inline void RequestReset()
{
	mainGSMStateMachine = SM_GSM_HW_FAULT;
}

static inline BOOL ModuleOnReset()
{
	return (mainGSM.HWReady == FALSE || mainGSMStateMachine == SM_GSM_HW_FAULT);
}

static void TCPHandleError(TCPClient_t *this)
{
	if (mainGSMStateMachine == SM_GSM_HW_FAULT)
		this->sock.number = INVALID_SOCKET;
	else
		TCPClient_stop(this);
}

static inline BOOL TCPInvalidSocket(TCPClient_t *this)
{
	return (this->sock.number == INVALID_SOCKET);
}

static int TCPCheckStatus(TCPClient_t *this)
{
	int len = 0;

	if (this->size > 0)
		return this->size;

	if (TCPInvalidSocket(this))
		return -1;

	len = this->sock.rxLen;
	
	if (len > 0) {
		sprintf(this->tmp, "RxLen:%d\r\n", len);
		UARTWrite(1, this->tmp);

		if (len > TCP_MAX_BUF_SIZE)
			len = TCP_MAX_BUF_SIZE;
		TCPRead(&this->sock, this->buff, len);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS)
		{
			UARTWrite(1, "Errors on reading TCP buffer!\r\n");	
			TCPHandleError(this);
			return -1;
		}

		this->size = len;
		this->idx = 0;
	}
	
	return len;
}

/**
* Connects to a specified IP address and port. 
* Also supports DNS lookups when using a domain name.
* The return value indicates success or failure. 
*/
BOOL TCPClient_connect(TCPClient_t *this, char *server, uint16_t port)
{
	TCPClient_init(this);

	while (1) 
	{
		vTaskDelay(200);
		if ((tickGetSeconds() - this->tick) > 600) {
			this->tick = tickGetSeconds();
			RequestReset();
		}
		if (ModuleOnReset()) {
			UARTWrite(1, "GPRS hardware not ready\r\n");
			continue;
		}
	    if ((LastConnStatus() != REG_SUCCESS) && (LastConnStatus() != ROAMING)) {
			UARTWrite(1, "Wait for GPRS Connection\r\n");
	    	continue;
	    }
		
		UARTWrite(1, "\r\nSetup APN params\r\n");
		APNConfig("cmnet", "", "", DYNAMIC_IP, DYNAMIC_IP, DYNAMIC_IP);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS) {
			UARTWrite(1, "Errors on APNConfig function!\r\n");	
			continue;
		}
		
		UARTWrite(1, "Connecting to TCP Server...\r\n");
		sprintf(this->tmp, "%d", port);
		this->sock.number = INVALID_SOCKET;
		TCPClientOpen(&this->sock, server, this->tmp);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS)
		{
			UARTWrite(1, "Errors on TCPClientOpen function!\r\n");	
			continue;
		}
		else if (!TCPInvalidSocket(this))
		{
			UARTWrite(1, "\r\nTCPClientOpen OK \r\n");
			UARTWrite(1, "Socket Number: ");
			sprintf(this->tmp, "%d\r\n", this->sock.number);
			UARTWrite(1, this->tmp);
			break;
		}
		else {
			UARTWrite(1, "TCPClientOpen Failed!\r\n");	
			continue;			
		}
	}
	
	return TRUE;
}

/**
* Disconnect from the server.
*/
void TCPClient_stop(TCPClient_t *this)
{
	if (TCPInvalidSocket(this))
		return;
	
	UARTWrite(1, "Closing socket...\r\n");
	TCPClientClose(&this->sock);
	
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
		UARTWrite(1, "Errors on TCPClientClose!\r\n");	
	else
		UARTWrite(1, "Socket Closed\r\n"); 

	this->sock.number = INVALID_SOCKET;
}

/**
* Returns the number of bytes available for reading,
* that is, the amount of data that has been written to the client by the server it is connected to.
*/
int TCPClient_available(TCPClient_t *this)
{
	int rxlen = TCPCheckStatus(this);
	if (rxlen > 0)
		return rxlen;
	return 0;
}

/**
* Write data to the server the client is connected to. This data is sent as a series of bytes.
* Returns the number of bytes written. 
*/
int TCPClient_write(TCPClient_t *this, uint8_t *buf, int len)
{
	if (TCPInvalidSocket(this))
		return 0;

	UARTWrite(1, "Sending data...\r\n");
	TCPWrite(&this->sock, (char*)buf, len);
	
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors sending TCP data!\r\n");	
		TCPHandleError(this);
		return 0;
	}	
	else
	{
		sprintf(this->tmp, "TxLen:%d\r\n", len);
		UARTWrite(1, this->tmp);
	}
	return len;
}

int TCPClient_read(TCPClient_t *this, uint8_t *buf, int len)
{
	int nbytes = (this->size > len) ? len : this->size;
	if (nbytes > 0) {
		memcpy(buf, this->buff + this->idx, nbytes);
		this->size -= nbytes;
		this->idx += nbytes;
	}
	return nbytes;
}

/**
* Reads the first byte of incoming data available (or -1 if no data is available)
*/
int TCPClient_readByte(TCPClient_t *this)
{
	if (this->size > 0) {
		uint8_t b = this->buff[this->idx];
		this->size--;
		this->idx++;
		return b;
	}
	return -1;
}

/**
* Whether or not the client is connected. 
* Note that a client is considered connected if the connection has been closed but there is still unread data.
*
* Returns true if the client is connected, false if not.
*/
BOOL TCPClient_connected(TCPClient_t *this)
{
	if (TCPInvalidSocket(this))
		return FALSE;

	if (TCPCheckStatus(this) == -1)
		return FALSE;

	return TRUE;
}

/**
* Discard any bytes that have been written to the client but not yet read.
*/
void TCPClient_flush(TCPClient_t *this)
{
	if (TCPInvalidSocket(this))
		return;

	TCPRxFlush(&this->sock);
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors flush socket rx buffer!\r\n");	
		TCPHandleError(this);
	}
}

