#ifndef MQTTCLIENT_H
#define MQTTCLIENT_H

#include "TCPClient.h"
#include "opt.h"

// MQTT_MAX_PACKET_SIZE : Maximum packet size
#ifndef MQTT_MAX_PACKET_SIZE
#define MQTT_MAX_PACKET_SIZE 512
#endif

// MQTT_MAX_TOPIC_LEN : Maximum topic length
#ifndef MQTT_MAX_TOPIC_LEN
#define MQTT_MAX_TOPIC_LEN 32
#endif

// MQTT_KEEPALIVE : keepAlive interval in Seconds
#ifndef MQTT_KEEPALIVE
#define MQTT_KEEPALIVE 30UL
#endif

#define MQTTPROTOCOLVERSION 3
#define MQTTCONNECT     1 << 4  // Client request to connect to Server
#define MQTTCONNACK     2 << 4  // Connect Acknowledgment
#define MQTTPUBLISH     3 << 4  // Publish message
#define MQTTPUBACK      4 << 4  // Publish Acknowledgment
#define MQTTPUBREC      5 << 4  // Publish Received (assured delivery part 1)
#define MQTTPUBREL      6 << 4  // Publish Release (assured delivery part 2)
#define MQTTPUBCOMP     7 << 4  // Publish Complete (assured delivery part 3)
#define MQTTSUBSCRIBE   8 << 4  // Client Subscribe request
#define MQTTSUBACK      9 << 4  // Subscribe Acknowledgment
#define MQTTUNSUBSCRIBE 10 << 4 // Client Unsubscribe request
#define MQTTUNSUBACK    11 << 4 // Unsubscribe Acknowledgment
#define MQTTPINGREQ     12 << 4 // PING Request
#define MQTTPINGRESP    13 << 4 // PING Response
#define MQTTDISCONNECT  14 << 4 // Client is Disconnecting
#define MQTTReserved    15 << 4 // Reserved

#define MQTTQOS0        (0 << 1)
#define MQTTQOS1        (1 << 1)
#define MQTTQOS2        (2 << 1)

typedef struct MQTTClient 
{
   TCPClient_t* _client;
   uint8_t buffer[MQTT_MAX_PACKET_SIZE];
   uint16_t nextMsgId;
   unsigned long lastOutActivity;
   unsigned long lastInActivity;
   BOOL pingOutstanding;
   void (*callback)(char*,uint8_t*,unsigned int);
   char* server;
   uint16_t port;
} MQTTClient_t;

void MQTTClient_init(MQTTClient_t *, char*, uint16_t, void(*)(char*,uint8_t*,unsigned int),TCPClient_t *);
BOOL MQTTClient_connect(MQTTClient_t *, char *, char *, char *, char *, uint8_t, uint8_t, char*);
void MQTTClient_disconnect(MQTTClient_t *);
BOOL MQTTClient_publish(MQTTClient_t *, char *, uint8_t *, unsigned int, BOOL);
BOOL MQTTClient_subscribe(MQTTClient_t *, char *);
BOOL MQTTClient_loop(MQTTClient_t *);
BOOL MQTTClient_connected(MQTTClient_t *);

#endif
