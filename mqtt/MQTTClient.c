#include "MQTTClient.h"

static uint16_t MQTTClient_readPacket(MQTTClient_t *this);
static BOOL MQTTClient_write(MQTTClient_t *this, uint8_t header, uint8_t* buf, uint16_t length);
static uint16_t MQTTClient_writeStr(char* string, uint8_t* buf, uint16_t pos);

/**
* Creates an MQTT client ready for connection to the specified server
*
* Parameters
* @server : the DNS name or IP addr of the server
* @ip : the IP address of the server
* @port : the port to connect to
* @callback : a pointer to a function called when a message arrives for a subscription created by this client. If no callback is required, set this to 0. See Subscription Callback.
* @client : an instance of Client, typically EthernetClient.
*/
void MQTTClient_init(MQTTClient_t *this, char* server, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), TCPClient_t *client)
{
   this->_client = client;
   this->callback = callback;
   this->server = server;
   this->port = port;
}

/**
* Connects the client with a Will message, username and password specified.
*
* Parameters
* @id : the client ID to use when connecting to the server. As per MQTT, this must be between 1 and 23 characters long.
* @user : the username to use. If NULL, no username or password is used
* @pass : the password to use. If NULL, no password is used
* @willTopic : the topic to be used by the will message
* @willQoS : the quality of service to be used by the will message (0,1 or 2)
* @willRetain : whether the will should be published with the retain flag (0 or 1)
* @willMessage : the payload of the will message 

* Returns
*  false - connection failed.
*  true -connection succeeded.
*/
BOOL MQTTClient_connect(MQTTClient_t *this, char *id, char *user, char *pass, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage)
{
   if (!MQTTClient_connected(this)) {
      TCPClient_t *client = this->_client;
      uint8_t *buffer = this->buffer;
      int result = TCPClient_connect(client, this->server, this->port);
		
      if (result) {
         this->nextMsgId = 1;
         uint8_t d[9] = {0x00,0x06,'M','Q','I','s','d','p',MQTTPROTOCOLVERSION};
         // Leave room in the buffer for header and variable length field
         uint16_t length = 5;
         unsigned int j;
         for (j = 0;j<9;j++) {
            buffer[length++] = d[j];
         }

         uint8_t v;
         if (willTopic) {
            v = 0x06|(willQos<<3)|(willRetain<<5);
         } else {
            v = 0x02;
         }

         if(user != NULL) {
            v = v|0x80;

            if(pass != NULL) {
               v = v|(0x80>>1);
            }
         }

         buffer[length++] = v;

         buffer[length++] = ((MQTT_KEEPALIVE) >> 8);
         buffer[length++] = ((MQTT_KEEPALIVE) & 0xFF);
         length = MQTTClient_writeStr(id,buffer,length);
         if (willTopic) {
            length = MQTTClient_writeStr(willTopic,buffer,length);
            length = MQTTClient_writeStr(willMessage,buffer,length);
         }

         if(user != NULL) {
            length = MQTTClient_writeStr(user,buffer,length);
            if(pass != NULL) {
               length = MQTTClient_writeStr(pass,buffer,length);
            }
         }
         
         MQTTClient_write(this,MQTTCONNECT,buffer,length-5);
         
         this->lastInActivity = this->lastOutActivity = tickGetSeconds();
         
         while (!TCPClient_available(client)) {
            unsigned long t = tickGetSeconds();
            if (t - this->lastInActivity > MQTT_KEEPALIVE) {
               TCPClient_stop(client);
               return FALSE;
            }
         }
         uint16_t len = MQTTClient_readPacket(this);
         if (len == 4 && buffer[3] == 0) {
            this->lastInActivity = tickGetSeconds();
            this->pingOutstanding = FALSE;
            return TRUE;
         }
      }
   }
   return FALSE;
}

static uint8_t MQTTClient_readByte(MQTTClient_t *this, int *err)
{
   int data = TCPClient_readByte(this->_client);
   if (data == -1) {
	   unsigned long t = tickGetSeconds();
	   while(!TCPClient_available(this->_client)) {
	      if (tickGetSeconds() - t > 1) {
	   		*err = 1;
			return 0;
	      }
	   }
	   data = TCPClient_readByte(this->_client);
   }
   
   return (uint8_t)data;
}

static uint16_t MQTTClient_readPacket(MQTTClient_t *this)
{
   uint8_t *buffer = this->buffer;
   uint16_t len = 0;
   uint8_t multiplier = 1;
   uint16_t length = 0;
   uint8_t digit = 0;
   uint16_t i;
   int err = 0;

   buffer[len++] = MQTTClient_readByte(this, &err);
   
   do {
      digit = MQTTClient_readByte(this, &err);
      if (err)
         return 0;
      buffer[len++] = digit;
      length += (digit & 127) * multiplier;
      multiplier *= 128;
   } while ((digit & 128) != 0);
   
   for (i = 0;i<length;i++)
   {
      if (len < MQTT_MAX_PACKET_SIZE) {
         buffer[len++] = MQTTClient_readByte(this, &err);
      } else {
         MQTTClient_readByte(this, &err);
         len = 0; // This will cause the packet to be ignored.
      }

	  if (err)
         return 0;
   }

   return len;
}

/**
* This should be called regularly to allow the client to process incoming messages and maintain its connection to the server.
*
* Returns
*  false - the client is no longer connected
*  true - the client is still connected
*/
BOOL MQTTClient_loop(MQTTClient_t *this)
{
   uint8_t *buffer = this->buffer;
   if (MQTTClient_connected(this)) {
      unsigned long t = tickGetSeconds();
      if ((t - this->lastInActivity > MQTT_KEEPALIVE) || (t - this->lastOutActivity > MQTT_KEEPALIVE)) {
         if (this->pingOutstanding) {
            TCPClient_stop(this->_client);
            return FALSE;
         } else {
            buffer[0] = MQTTPINGREQ;
            buffer[1] = 0;
            if (0 == TCPClient_write(this->_client, buffer, 2))
			   return FALSE;
            this->lastOutActivity = t;
            this->lastInActivity = t;
            this->pingOutstanding = TRUE;
         }
      }
      if (TCPClient_available(this->_client)) {
         uint16_t len = MQTTClient_readPacket(this);
         if (len > 0) {
            this->lastInActivity = t;
            uint8_t type = buffer[0]&0xF0;
            if (type == MQTTPUBLISH) {
               if (this->callback) {
                  uint16_t tl, offset;
                  if (len < 128) {
                     tl = (buffer[2]<<8)+buffer[3];
                     offset = 4;
                  }
				  else {
                     tl = (buffer[3]<<8)+buffer[4];
                     offset = 5;
				  }
                  char topic[MQTT_MAX_TOPIC_LEN + 1];
				  uint16_t i;
				  if (tl > MQTT_MAX_TOPIC_LEN)
				     return TRUE;
                  for (i=0;i<tl;i++) {
                     topic[i] = buffer[offset+i];
                  }
                  topic[tl] = 0;
                  // ignore msgID - only support QoS 0 subs
                  uint8_t *payload = buffer+offset+tl;
                  this->callback(topic,payload,len-offset-tl);
               }
            } else if (type == MQTTPINGREQ) {
               buffer[0] = MQTTPINGRESP;
               buffer[1] = 0;
               if (0 == TCPClient_write(this->_client,buffer,2))
			      return FALSE;
            } else if (type == MQTTPINGRESP) {
               this->pingOutstanding = FALSE;
            } else {
               TCPClient_flush(this->_client);
            }
         }
      }
      return TRUE;
   }
   return FALSE;
}

/**
* Publishes a message to the specified topic, with the retained flag as specified.
* The message is published at QoS 0.

* Parameters
* @topic : the topic to publish to
* @payload : the message to publish
* @length : the length of the message
* @retained : whether the message should be retained
*  0 - not retained
*  1 - retained
* Returns
*  false -  publish failed.
*  true - publish succeeded.
*/
BOOL MQTTClient_publish(MQTTClient_t *this, char* topic, uint8_t* payload, unsigned int plength, BOOL retained)
{
   if (MQTTClient_connected(this)) {
      uint8_t *buffer = this->buffer;
      // Leave room in the buffer for header and variable length field
      uint16_t length = 5;
      length = MQTTClient_writeStr(topic,buffer,length);
      uint16_t i;
      for (i=0;i<plength;i++) {
         buffer[length++] = payload[i];
      }
      uint8_t header = MQTTPUBLISH;
      if (retained) {
         header |= 1;
      }
      return MQTTClient_write(this,header,buffer,length-5);
   }
   return FALSE;
}

static BOOL MQTTClient_write(MQTTClient_t *this, uint8_t header, uint8_t* buf, uint16_t length)
{
   uint8_t lenBuf[4];
   uint8_t llen = 0;
   uint8_t digit;
   uint8_t pos = 0;
   uint8_t rc;
   uint8_t len = length;
   int i;
   
   do {
      digit = len % 128;
      len = len / 128;
      if (len > 0) {
         digit |= 0x80;
      }
      lenBuf[pos++] = digit;
      llen++;
   } while(len>0);

   buf[4-llen] = header;
   for (i=0;i<llen;i++) {
      buf[5-llen+i] = lenBuf[i];
   }
   rc = TCPClient_write(this->_client,buf+(4-llen),length+1+llen);
   
   this->lastOutActivity = tickGetSeconds();
   return (rc == 1+llen+length);
}

/**
* Subscribes to messages published to the specified topic.

* Parameters
* @topic  : the topic to publish to
* Returns
*  false - sending the subscribe failed.
*  true - sending the subscribe succeeded. The request completes asynchronously.
*/
BOOL MQTTClient_subscribe(MQTTClient_t *this, char* topic)
{
   if (MQTTClient_connected(this)) {
      uint8_t *buffer = this->buffer;
      // Leave room in the buffer for header and variable length field
      uint16_t length = 7;
      this->nextMsgId++;
      if (this->nextMsgId == 0) {
         this->nextMsgId = 1;
      }
      buffer[0] = (this->nextMsgId >> 8);
      buffer[1] = (this->nextMsgId & 0xFF);
      length = MQTTClient_writeStr(topic, buffer,length);
      buffer[length++] = 0; // Only do QoS 0 subs
      return MQTTClient_write(this,MQTTSUBSCRIBE|MQTTQOS1,buffer,length-5);
   }
   return FALSE;
}

/**
* Disconnects the client.
*/
void MQTTClient_disconnect(MQTTClient_t *this)
{
   if (!TCPClient_connected(this->_client))
      return;
   this->buffer[0] = MQTTDISCONNECT;
   this->buffer[1] = 0;
   TCPClient_write(this->_client,this->buffer,2);
   TCPClient_stop(this->_client);
   this->lastInActivity = this->lastOutActivity = tickGetSeconds();
}

static uint16_t MQTTClient_writeStr(char* string, uint8_t* buf, uint16_t pos)
{
   char* idp = string;
   uint16_t i = 0;
   pos += 2;
   while (*idp) {
      buf[pos++] = *idp++;
      i++;
   }
   buf[pos-i-2] = (i >> 8);
   buf[pos-i-1] = (i & 0xFF);
   return pos;
}

/**
* Checks whether the client is connected to the server.

* Returns
*  false - the client is no longer connected
*  true - the client is still connected
*/
BOOL MQTTClient_connected(MQTTClient_t *this)
{
   return TCPClient_connected(this->_client);
}

