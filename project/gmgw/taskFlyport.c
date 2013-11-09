#include "taskFlyport.h"
#include "taskModbus.h"
#include "MQTTClient.h"
#include "RS485Helper.h"
#include "ini.h"
#include "mb.h"

xTaskHandle hModbusTask = NULL;
xQueueHandle xQueueModbus;
xQueueHandle xQueueMqtt;

#define DEVICE_ID_LENGTH 14

#define MQTT_SERVER "119.97.184.140" //"m2m.eclipse.org"
#define MQTT_PORT   1883//61613 //1883
#define MQTT_USER   "admin"
#define MQTT_PASS   "password"

#define MQTT_TOPIC_ADVT    "/advt"
#define MQTT_TOPIC_FEED    "/feed"
#define MQTT_TOPIC_CFG_REQ "/cfg/req"
#define MQTT_TOPIC_CFG_RSP "/cfg/rsp"
#define MQTT_TOPIC_CMD_REQ "/cmd/req"
#define MQTT_TOPIC_CMD_RSP "/cmd/rsp"

MQTTClient_t mqtt;
TCPClient_t client;

#define		DE_485		p2
#define		RE_485		p17
#define		TX_485		p5
#define		RX_485		p7
#define		TX_232		p4
#define		RX_232		p6
#define		CTS_232		p11
#define		RTS_232		p9

const int port485 = 	2;
const int port232 = 	3;

sys_config_t config; 
int init;

static int config_section_modbus(const char* name, const char* value)
{
	if (!strcmp(name, "mode")) {
		if (!strcmp(value, "ascii"))
			config.mode = MB_ASCII;
		else
			config.mode = MB_RTU;
	}
	else if (!strcmp(name, "port")) {
		config.port = atoi(value);
	}
	else if (!strcmp(name, "baud")) {
		config.baudrate = atoi(value);
	}
	else if (!strcmp(name, "data")) {
		config.dataBits = atoi(value);
	}
	else if (!strcmp(name, "parity")) {
		if (!strcmp(value, "even"))
			config.parity = MB_PAR_EVEN;
		else if (!strcmp(value, "odd"))
			config.parity = MB_PAR_ODD;
		else
			config.parity = MB_PAR_NONE;
	}
	else if (!strcmp(name, "stop")) {
		config.stopBits = atoi(value);
	}
	else if (!strcmp(name, "sid")) {
		int var, i = 0;
		const char deli[] = ",";
		char *token = strtok((char *)value, deli);
		while (token != NULL) {
			sscanf (token, "%d", &var);
			config.slave[i++] = var;
			token = strtok(NULL, deli);
		}
		config.nSlaves = i;
	}
	else {
		return 0;
	}
	return 1;
}

static int config_section_poll(const char* name, const char* value)
{
	if (config.nTasks >= MAX_NUM_POLL_TASKS)
		return 0;
	
	poll_cfg_t *poll = &config.pollTask[config.nTasks];

	if (!strcmp(name, "fid")) {
		poll->feedId = atoi(value);
	}
	else if (!strcmp(name, "func")) {
		poll->funCode = atoi(value);
	}
	else if (!strcmp(name, "reg")) {
		poll->regStart = atoi(value);
	}
	else if (!strcmp(name, "num")) {
		poll->nRegs = atoi(value);
	}
	else if (!strcmp(name, "freq")) {
		poll->period = atoi(value);
		poll->lasttime = tickGetSeconds();
		config.nTasks++;
	}
	else {
		return 0;
	}
	return 1;
}

static int config_handler(void* user, const char* section, const char* name, const char* value)
{
	UARTWrite(1, (char*)section);
	UARTWrite(1,": ");
	UARTWrite(1, (char*)name);
	UARTWrite(1,"=");
	UARTWrite(1, (char*)value);
	UARTWrite(1,"\r\n");

	if (!strcmp(section, "modbus")) {
		return config_section_modbus(name, value);
	}
	else if (!strcmp(section, "poll")) {
		return config_section_poll(name, value);
	}
	else {
		return 0;  /* unknown section/name, error */
	}
    return 1;
}

static void do_config(char *cfg, unsigned int len)
{
	init = 0;
	config.nTasks = 0;
#if 0
	config.mode = MB_RTU;
	config.port = port485;
	config.baudrate = 19200;
	config.dataBits = 8;
	config.parity = MB_PAR_EVEN;
	config.stopBits = 1;
#endif	
	if (ini_parse(cfg, len, config_handler, NULL) < 0) {
		UARTWrite(1, "Invalid configuration.\n");
		return;
	}

	if (eMBInit(config.mode, 0x0A, config.port, config.baudrate, config.dataBits, config.parity, config.stopBits)) {
		UARTWrite(1, "Failed to init Modbus.\n");
		return;
	}
	
	if (eMBEnable()) {
		UARTWrite(1, "Failed to enable Modbus.\n");
		return;
	}

	init = 1;
}

static BOOL mqtt_send_msg(char* topic, uint8_t* payload, unsigned int length)
{
	char tp[MQTT_MAX_TOPIC_LEN + 1];
	char *devid = GSMGetIMEI();
	sprintf(tp, "%s%s", devid, topic);
	return MQTTClient_publish(&mqtt, tp, payload, length, FALSE);
}

static void mqtt_callback(char* topic, uint8_t* payload, unsigned int length)
{ 
	char sLen[5];
	sprintf(sLen, "%d", length);
	UARTWrite(1,"mqtt_callback, len=");
	UARTWrite(1,sLen);
	UARTWrite(1," topic=");
	UARTWrite(1,topic);
	UARTWrite(1,"\r\n");

	if (!strcmp(topic + DEVICE_ID_LENGTH, MQTT_TOPIC_CMD_REQ)) {
		msg_hdr_t *msg = (msg_hdr_t *)(payload - 4);
		msg->msg_type = MSG_CMD_REQ;
		msg->data_len = length;
		xQueueSend(xQueueModbus, msg, portTICK_RATE_MS * 2000);
		return;
	}

	if (!strcmp(topic + DEVICE_ID_LENGTH, MQTT_TOPIC_CFG_REQ)) {
		vTaskSuspend(hModbusTask);
		do_config((char*)payload, length);
		if (init)
			mqtt_send_msg(MQTT_TOPIC_CFG_RSP, (uint8_t*)"OK", 2);
		else
			mqtt_send_msg(MQTT_TOPIC_CFG_RSP, (uint8_t*)"ERROR", 5);
		vTaskResume(hModbusTask);
		return;
	}

	UARTWrite(1, "Unknown topic!\r\n");
}

void FlyportTask()
{
	init = 0;
	int connected = 0;
	unsigned long rssi_lastime = 0;
	unsigned long ad_lastime = 0;
	char ad_info[80];
	msg_hdr_t *msg = (msg_hdr_t *)(mqtt.buffer + 50);
	
	// Initialize the RS485
	RS485Off(port485);
	RS485Remap(port485, TX_485, RX_485, DE_485, RE_485);
	
	vTaskDelay(20);
    UARTWrite(1,"Flyport Task Started...\r\n");

	if (hModbusTask == NULL) {
		xQueueModbus = xQueueCreate(1, EXTRA_HEAD_ROOM + MB_SER_PDU_SIZE_MAX);
		xQueueMqtt = xQueueCreate(1, MB_SER_PDU_SIZE_MAX);
		// Creates the task dedicated to user code
		xTaskCreate(TaskModbus,(signed char*) "MODBUS" , (configMINIMAL_STACK_SIZE * 4), 
			NULL, tskIDLE_PRIORITY + 1, &hModbusTask);	
	}
	
	// Wait for GSM Connection successfull
    while((LastConnStatus() != REG_SUCCESS) && (LastConnStatus() != ROAMING)) {
    	vTaskDelay(20);
    	IOPut(p21, toggle);
    }
    IOPut(p21, on);
	vTaskDelay(20);
    UARTWrite(1,"Flyport registered on network!\r\n");

	sprintf(ad_info, "[gw]\nmft=GeeLink\nmdl=GPRS\nsn=%s\nhw=1.0\nsw=1.0", GSMGetIMEI());

	TCPClient_init(&client);
	MQTTClient_init(&mqtt, MQTT_SERVER, MQTT_PORT, mqtt_callback, &client);

	while (1) {
		if (!connected) {
			init = 0;
			vTaskDelay(1000);
			char *devid = GSMGetIMEI();
	
			// clientID, username, MD5 encoded password
			if (!MQTTClient_connect(&mqtt, devid, MQTT_USER, MQTT_PASS, NULL, 0, 0, NULL)) {
				UARTWrite(1,"Failed connect to mqtt server!\r\n");
				continue;
			}
			connected = 1;
			UARTWrite(1,"Connected to mqtt server!\r\n");
			
			char topic[MQTT_MAX_TOPIC_LEN + 1];
			sprintf(topic, "%s%s", devid, MQTT_TOPIC_CFG_REQ);
			MQTTClient_subscribe(&mqtt, topic);
			UARTWrite(1,"Subscribed mqtt topic: ");
			UARTWrite(1, topic);
			UARTWrite(1,"\r\n");
			sprintf(topic, "%s%s", devid, MQTT_TOPIC_CMD_REQ);
			MQTTClient_subscribe(&mqtt, topic);
			UARTWrite(1,"Subscribed mqtt topic: ");
			UARTWrite(1, topic);
			UARTWrite(1,"\r\n");
		}
		else if (!init) {
			if (tickGetSeconds() > (ad_lastime + 30)) {
				IOPut(p21, toggle);
				mqtt_send_msg(MQTT_TOPIC_ADVT, (uint8_t*)ad_info, strlen(ad_info));
				ad_lastime = tickGetSeconds();
			}
		}

		if (xQueueReceive(xQueueMqtt, (void *)msg, 0)) {
			if (msg->msg_type == MSG_FEED)
				mqtt_send_msg(MQTT_TOPIC_FEED, (uint8_t*)&msg->feedid, msg->data_len);
			else
				mqtt_send_msg(MQTT_TOPIC_CMD_RSP, (uint8_t*)&msg->seqno[0], msg->data_len);
		}

		if (!MQTTClient_loop(&mqtt)) {
			MQTTClient_disconnect(&mqtt);
			connected = 0;
		}

		if (connected && tickGetSeconds() > (rssi_lastime + 5)) {
			GSMSignal();
			while(LastExecStat() == OP_EXECUTION)
				vTaskDelay(1);
			char rssi[12];
			sprintf(rssi, "RSSI:%d\r\n", GSMGetRSSI());
			UARTWrite(1, rssi);	
			rssi_lastime = tickGetSeconds();
		}
	}
}


