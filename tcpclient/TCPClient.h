#ifndef TCPCLIENT_H
#define TCPCLIENT_H

#include "taskFlyport.h"
#include "opt.h"

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;

#define tickGetSeconds TickGetDiv64K

#ifndef TCP_MAX_BUF_SIZE
#define TCP_MAX_BUF_SIZE 512
#endif

typedef struct TCPClient 
{
	TCP_SOCKET sock;
	char buff[TCP_MAX_BUF_SIZE + 1];
	char tmp[16];
	uint16_t size;
	uint16_t idx; 
	int retries;
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
