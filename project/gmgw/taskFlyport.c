#include "taskFlyport.h"
#include "MQTTClient.h"

#define MQTT_SERVER "m2m.eclipse.org"
#define MQTT_TOPIC "jesse/light"
#define HELLO_MESSAGE "I'm alive!"
#define ON_MESSAGE "{\"report\":{\"light\": \"ON\"}}"
#define OFF_MESSAGE "{\"report\":{\"light\": \"OFF\"}}"
MQTTClient_t mqtt;
TCPClient_t client;
unsigned long lasttime = 0;
/////////////////////////////////////////////////////////////////////////////
// defines and variable for sensor/control mode
#define MODE_OFF    0  // not sensing light, LED off
#define MODE_ON     1  // not sensing light, LED on
#define MODE_SENSE  2  // sensing light, LED controlled by software
int senseMode = 0;

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length)
{ 
  char tmp[100];
  sprintf(tmp, "%d topic=%s", length, topic);
  UARTWrite(1,"mqtt_callback, len=");
  UARTWrite(1,tmp);
  UARTWrite(1,"\r\n");
  if (!memcmp(payload, "{\"command\":{\"lightmode\": \"OFF\"}}", length)) {
    senseMode = MODE_OFF;
	UARTWrite(1,"==Light OFF\r\n");
  } else if (!memcmp(payload, "{\"command\":{\"lightmode\": \"ON\"}}", length)) {
    senseMode = MODE_ON;
	UARTWrite(1,"==Light ON\r\n");
  }
}

void FlyportTask()
{	
	vTaskDelay(20);
    UARTWrite(1,"Flyport Task Started...\r\n");
	
	// Wait for GSM Connection successfull
    while((LastConnStatus() != REG_SUCCESS) && (LastConnStatus() != ROAMING))
    {
    	vTaskDelay(20);
    	IOPut(p21, toggle);
    }
    IOPut(p21, on);
	vTaskDelay(20);
    UARTWrite(1,"Flyport registered on network!\r\n");
//////////////////////////////////////

	static int connected = 0;

	TCPClient_init(&client);
	MQTTClient_init(&mqtt, MQTT_SERVER, 1883, mqtt_callback, &client);

	while (1) {
		if (!connected)
		{
			vTaskDelay(500);
			// clientID, username, MD5 encoded password
			if (!MQTTClient_connect(&mqtt, "jesse@m2m", NULL, NULL, NULL, 0, 0, NULL)) {
				UARTWrite(1,"Failed connect to mqtt server!\r\n");
				continue;
			}
			connected = 1;
			UARTWrite(1,"Connected to mqtt server!\r\n");
			
			MQTTClient_publish(&mqtt, MQTT_TOPIC, (uint8_t*)HELLO_MESSAGE, strlen(HELLO_MESSAGE), FALSE);
			MQTTClient_subscribe(&mqtt, MQTT_TOPIC);
		}

		// publish light reading every 10 seconds
		if (tickGetSeconds() > (lasttime + 10)) {
			lasttime = tickGetSeconds();
			if (senseMode == MODE_OFF)
				MQTTClient_publish(&mqtt, MQTT_TOPIC, (uint8_t*)OFF_MESSAGE, strlen(OFF_MESSAGE), FALSE);
			else if (senseMode == MODE_ON)
				MQTTClient_publish(&mqtt, MQTT_TOPIC, (uint8_t*)ON_MESSAGE, strlen(ON_MESSAGE), FALSE);
		}

		if (!MQTTClient_loop(&mqtt)) {
			MQTTClient_disconnect(&mqtt);
			connected = 0;
		}

		IOPut(p21, toggle);
	}
}


