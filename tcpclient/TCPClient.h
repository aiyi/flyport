#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include "taskFlyport.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

#define tickGetSeconds TickGetDiv64K


typedef struct TCPClient 
{
	TCP_SOCKET sock;
	char buff[1500];
	uint16_t size;
	uint16_t idx; 
	unsigned long lastCheck;
} TCPClient_t;

void TCPClient_init(TCPClient_t *);
BOOL TCPClient_connect(TCPClient_t *, char *, uint16_t);
void TCPClient_stop(TCPClient_t *);
int TCPClient_available(TCPClient_t *);
int TCPClient_write(TCPClient_t *, uint8_t *, int);
int TCPClient_read(TCPClient_t *, uint8_t *, int);
int TCPClient_readByte(TCPClient_t *);
BOOL TCPClient_connected(TCPClient_t *);
void TCPClient_flush(TCPClient_t *);

#endif
