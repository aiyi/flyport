#include "MQTTClient.h"

static uint16_t MQTTClient_readPacket(MQTTClient_t *this);
static boolean MQTTClient_write(MQTTClient_t *this, uint8_t header, uint8_t* buf, uint16_t length)
static uint16_t MQTTClient_writeStr(char* string, uint8_t* buf, uint16_t pos);

/**
* Creates an MQTT client ready for connection to the specified server
*
* Parameters
* @domain : the DNS name of the server
* @ip : the IP address of the server
* @port : the port to connect to
* @callback : a pointer to a function called when a message arrives for a subscription created by this client. If no callback is required, set this to 0. See Subscription Callback.
* @client : an instance of Client, typically EthernetClient.
*/
void MQTTClient_init(MQTTClient_t *this, char* domain, uint8_t *ip, uint16_t port, void (*callback)(char*,uint8_t*,unsigned int), TCPClient_t *client)
{
   this->_client = client;
   this->callback = callback;
   this->domain = domain;
   this->ip = ip;
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
*  false – connection failed.
*  true – connection succeeded.
*/
boolean MQTTClient_connect(MQTTClient_t *this, char *id, char *user, char *pass, char* willTopic, uint8_t willQos, uint8_t willRetain, char* willMessage)
{
   if (!MQTTClient_connected(this)) {
      TCPClient_t *client = this->_client;
      uint8_t *buffer = this->buffer;
      int result = TCPClient_connect(this->_client, this->domain, this->ip, this->port);
		
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
         
         this->lastInActivity = this->lastOutActivity = millis();
         
         while (!client->available()) {
            unsigned long t = millis();
            if (this->lastInActivity > MQTT_KEEPALIVE*1000UL) {
               client->stop();
               return false;
            }
         }
         uint16_t len = MQTTClient_readPacket(this);
         
         if (len == 4 && buffer[3] == 0) {
            this->lastInActivity = millis();
            this->pingOutstanding = false;
            return true;
         }
      }
      client->stop();
   }
   return false;
}

static uint8_t MQTTClient_readByte(MQTTClient_t *this)
{
   while(!this->_client->available()) {}
   return this->_client->read();
}

static uint16_t MQTTClient_readPacket(MQTTClient_t *this)
{
   uint8_t *buffer = this->buffer;
   uint16_t len = 0;
   buffer[len++] = MQTTClient_readByte(this);
   uint8_t multiplier = 1;
   uint16_t length = 0;
   uint8_t digit = 0;
   do {
      digit = MQTTClient_readByte(this);
      buffer[len++] = digit;
      length += (digit & 127) * multiplier;
      multiplier *= 128;
   } while ((digit & 128) != 0);
   
   for (uint16_t i = 0;i<length;i++)
   {
      if (len < MQTT_MAX_PACKET_SIZE) {
         buffer[len++] = MQTTClient_readByte(this);
      } else {
         MQTTClient_readByte(this);
         len = 0; // This will cause the packet to be ignored.
      }
   }

   return len;
}

/**
* This should be called regularly to allow the client to process incoming messages and maintain its connection to the server.
*
* Returns
*  false – the client is no longer connected
*  true – the client is still connected
*/
boolean MQTTClient_loop(MQTTClient_t *this)
{
   uint8_t *buffer = this->buffer;
   if (MQTTClient_connected(this)) {
      unsigned long t = millis();
      if ((t - this->lastInActivity > MQTT_KEEPALIVE*1000UL) || (t - this->lastOutActivity > MQTT_KEEPALIVE*1000UL)) {
         if (this->pingOutstanding) {
            this->_client->stop();
            return false;
         } else {
            buffer[0] = MQTTPINGREQ;
            buffer[1] = 0;
            this->_client->write(buffer,2);
            this->lastOutActivity = t;
            this->lastInActivity = t;
            this->pingOutstanding = true;
         }
      }
      if (this->_client->available()) {
         uint16_t len = readPacket();
         if (len > 0) {
            this->lastInActivity = t;
            uint8_t type = buffer[0]&0xF0;
            if (type == MQTTPUBLISH) {
               if (this->callback) {
                  uint16_t tl = (buffer[2]<<8)+buffer[3];
                  char topic[tl+1];
                  for (uint16_t i=0;i<tl;i++) {
                     topic[i] = buffer[4+i];
                  }
                  topic[tl] = 0;
                  // ignore msgID - only support QoS 0 subs
                  uint8_t *payload = buffer+4+tl;
                  this->callback(topic,payload,len-4-tl);
               }
            } else if (type == MQTTPINGREQ) {
               buffer[0] = MQTTPINGRESP;
               buffer[1] = 0;
               _client->write(buffer,2);
            } else if (type == MQTTPINGRESP) {
               this->pingOutstanding = false;
            }
         }
      }
      return true;
   }
   return false;
}

/**
* Publishes a message to the specified topic, with the retained flag as specified.
* The message is published at QoS 0.

* Parameters
* @topic – the topic to publish to
* @payload – the message to publish
* @length – the length of the message
* @retained – whether the message should be retained
*  0 – not retained
*  1 – retained
* Returns
*  false – publish failed.
*  true – publish succeeded.
*/
boolean MQTTClient_publish(MQTTClient_t *this, char* topic, uint8_t* payload, unsigned int plength, boolean retained)
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
   return false;
}

static boolean MQTTClient_write(MQTTClient_t *this, uint8_t header, uint8_t* buf, uint16_t length)
{
   uint8_t lenBuf[4];
   uint8_t llen = 0;
   uint8_t digit;
   uint8_t pos = 0;
   uint8_t rc;
   uint8_t len = length;
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
   for (int i=0;i<llen;i++) {
      buf[5-llen+i] = lenBuf[i];
   }
   rc = this->_client->write(buf+(4-llen),length+1+llen);
   
   this->lastOutActivity = millis();
   return (rc == 1+llen+length);
}

/**
* Subscribes to messages published to the specified topic.

* Parameters
* @topic – the topic to publish to
* Returns
*  false – sending the subscribe failed.
*  true – sending the subscribe succeeded. The request completes asynchronously.
*/
boolean MQTTClient_subscribe(MQTTClient_t *this, char* topic)
{
   if (MQTTClient_connected(this)) {
      uint8_t *buffer = this->buffer;
      // Leave room in the buffer for header and variable length field
      uint16_t length = 7;
      nextMsgId++;
      if (nextMsgId == 0) {
         nextMsgId = 1;
      }
      buffer[0] = (nextMsgId >> 8);
      buffer[1] = (nextMsgId & 0xFF);
      length = MQTTClient_writeStr(topic, buffer,length);
      buffer[length++] = 0; // Only do QoS 0 subs
      return MQTTClient_write(this,MQTTSUBSCRIBE|MQTTQOS1,buffer,length-5);
   }
   return false;
}

/**
* Disconnects the client.
*/
void MQTTClient_disconnect(MQTTClient_t *this)
{
   this->buffer[0] = MQTTDISCONNECT;
   this->buffer[1] = 0;
   this->_client->write(this->buffer,2);
   this->_client->stop();
   this->lastInActivity = this->lastOutActivity = millis();
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
*  false – the client is no longer connected
*  true – the client is still connected
*/
boolean MQTTClient_connected(MQTTClient_t *this)
{
   int rc = (int)this->_client->connected();
   if (!rc) this->_client->stop();
   return rc;
}

