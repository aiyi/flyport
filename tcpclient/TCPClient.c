#include "TCPClient.h"
#include "GSMData.h"

void TCPClient_init(TCPClient_t *this)
{
	this->sock.number = INVALID_SOCKET;
	this->size = 0;
	this->idx = 0;
	this->lastCheck = 0;
}

static int TCPClient_status(TCPClient_t *this)
{
	int len = 0;

	if (this->size > 0)
		return this->size;

	if (this->sock.number == INVALID_SOCKET)
		return -1;

	if (tickGetSeconds() - this->lastCheck < 1UL)
		return 0;

	UARTWrite(1, "Updating Socket Status...\r\n");
	TCPStatus(&this->sock);
	
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors on updating TCPStatus!\r\n");
		this->sock.number = INVALID_SOCKET;
		return -1;
	}
	else
	{
		len = this->sock.rxLen;
		if (len != 0 || this->sock.status != 3) 
		{
			char temp[8];
			UARTWrite(1, " Status:");
			sprintf(temp, "%d, ", this->sock.status);
			UARTWrite(1, temp);
			UARTWrite(1, "RxLen:");
			sprintf(temp, "%d\r\n", len);
			UARTWrite(1, temp);
		}
	}

	if (len > 0) {
		TCPRead(&this->sock, this->buff, len);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS)
		{
			UARTWrite(1, "Errors on reading TCP buffer!\r\n");	
			this->sock.number = INVALID_SOCKET;
			return -1;
		}	
	}
	
	this->size = len;
	this->idx = 0;
	this->lastCheck = tickGetSeconds();
	return len;
}

extern GSMModule mainGSM;

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
		char temp[8];
		this->sock.number = INVALID_SOCKET;
		vTaskDelay(100);
		if(mainGSM.HWReady != TRUE)
			continue;
		
		UARTWrite(1, "\r\n\r\nSetup APN params\r\n");
		APNConfig("cmnet", "", "", DYNAMIC_IP, DYNAMIC_IP, DYNAMIC_IP);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS)
			continue;

		UARTWrite(1, "Connecting to TCP Server...");
		sprintf(temp, "%d", port);
		TCPClientOpen(&this->sock, server, temp);
		
		while(LastExecStat() == OP_EXECUTION)
			vTaskDelay(1);
		if(LastExecStat() != OP_SUCCESS)
		{
			UARTWrite(1, "Errors on TCPClientOpen function!\r\n");	
			continue;
		}
		else
		{
			UARTWrite(1, "\r\n TCPClientOpen OK \r\n");
			UARTWrite(1, "Socket Number: ");
			sprintf(temp, "%d\r\n", this->sock.number);
			UARTWrite(1, temp);	
		}

		if (TCPClient_status(this) != -1)
			break;
	}
	
	return TRUE;
}

/**
* Disconnect from the server.
*/
void TCPClient_stop(TCPClient_t *this)
{
	if (this->sock.number == INVALID_SOCKET)
		return;
	
	UARTWrite(1, "Closing socket...\r\n");
	TCPClientClose(&this->sock);
	
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
		UARTWrite(1, "Errors on TCPClientClose!\r\n");	
	else
		UARTWrite(1, "Socket Closed!\r\n"); 

	this->sock.number = INVALID_SOCKET;
}

/**
* Returns the number of bytes available for reading,
* that is, the amount of data that has been written to the client by the server it is connected to.
*/
int TCPClient_available(TCPClient_t *this)
{
	int rxlen = TCPClient_status(this);
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
	if (this->sock.number == INVALID_SOCKET)
		return 0;

	UARTWrite(1, "Sending data...");
	TCPWrite(&this->sock, (char*)buf, len);
	
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors sending TCP data!\r\n");	
		this->sock.number = INVALID_SOCKET;
		return 0;
	}	
	else
	{
		char temp[8];
		UARTWrite(1, "\r\n Status:");
		sprintf(temp, "%d, ", this->sock.status);
		UARTWrite(1, temp);
		UARTWrite(1, "TxLen:");
		sprintf(temp, "%d\r\n", len);
		UARTWrite(1, temp);
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
	if (this->sock.number == INVALID_SOCKET)
		return FALSE;

	if (TCPClient_status(this) == -1)
		return FALSE;

	return TRUE;
}

/**
* Discard any bytes that have been written to the client but not yet read.
*/
void TCPClient_flush(TCPClient_t *this)
{
	if (this->sock.number == INVALID_SOCKET)
		return;

	TCPRxFlush(&this->sock);
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors flush socket rx buffer!\r\n");	
		this->sock.number = INVALID_SOCKET;
	}
}

