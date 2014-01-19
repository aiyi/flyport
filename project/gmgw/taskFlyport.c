#include "taskFlyport.h"
#include "taskModbus.h"
#include "MQTTClient.h"
#include "RS485Helper.h"
#include "ini.h"
#include "mb.h"
#include "hashes.h"

extern int gsmDebugOn;

xTaskHandle hModbusTask = NULL;
xQueueHandle xQueueModbus;
xQueueHandle xQueueMqtt;

#define DEVICE_ID_LENGTH 14

#define MQTT_SERVER "119.97.184.6"
#define MQTT_PORT   1883
#define MQTT_USER   "admin"
#define MQTT_PASS   "password"

#define MQTT_TOPIC_ADVT    "/advt"
#define MQTT_TOPIC_FEED    "/feed"
#define MQTT_TOPIC_CFG_REQ "/cfg/req"
#define MQTT_TOPIC_CFG_RSP "/cfg/rsp"
#define MQTT_TOPIC_CMD_REQ "/cmd/req"
#define MQTT_TOPIC_CMD_RSP "/cmd/rsp"
#define MQTT_TOPIC_UPGRADE "/upgrade"

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

static char fileName[32];
static long fileSize;

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

static int upgrade_handler(void* user, const char* section, const char* name, const char* value)
{
	UARTWrite(1, (char*)name);
	UARTWrite(1,"=");
	UARTWrite(1, (char*)value);
	UARTWrite(1,"\r\n");

	if (!strcmp(name, "file")) {
		strcpy(fileName, value);
	}
	else if (!strcmp(name, "size")) {
		fileSize = atol(value);
	}
	else {
		return 0;  /* unknown section/name, error */
	}
    return 1;
}

static void do_upgrade(char *cfg, unsigned int len)
{
	fileSize = 256528;
	
	if (ini_parse(cfg, len, upgrade_handler, NULL) < 0) {
		UARTWrite(1, "Invalid firmware info.\n");
		return;
	}

	//	----- DOWNLOADING THE NEW FIRMWARE FILE	-----	
	FTP_SOCKET ftpSocket;
	ftpSocket.number = INVALID_SOCKET;
	FTPConfig(&ftpSocket, "119.97.184.140", "user", "pass", 21);
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
	{
		UARTWrite(1, "Errors on FTPConfig function!\r\n");
		return;
	}
	
	gsmDebugOn = 0;
	FTPReceive(&ftpSocket, 0x1C0000, "/", fileName, fileSize);
	while(LastExecStat() == OP_EXECUTION)
		vTaskDelay(1);
	if(LastExecStat() != OP_SUCCESS)
		UARTWrite(1, "ERROR in download firmware!\r\n");
	else {
		UARTWrite(1, "OK - Firmware downloaded!\r\n");
		//	MD5 INTEGRITY CHECK ON MEMORY
		BYTE resmd[16];
		HASH_SUM Hash;
		UARTWrite(1, "\r\nCalculating md5 from memory...\r\n");
		MD5Initialize (&Hash);
		long unsigned int f_ind = 0;
		BYTE b_read[2];
		for (f_ind = 0; f_ind < 256512; f_ind++)
		{
			SPIFlashReadArray(0x1c0000+f_ind, b_read, 1);
			HashAddData (&Hash, b_read, 1);
		}
		MD5Calculate(&Hash, resmd);
		
		int i;
		BYTE md5[16];
		SPIFlashReadArray(0x1c0000+256512, md5, 16);
		
		for (i=0; i<16; i++)
		{
			if (resmd[i] != md5[i])
			{
				UARTWrite(1, "ERROR - Firmware NOT valid!\r\n");
				break;
			}
		}
		if (i == 16) {
			UARTWrite(1, "OK - Firmware valid!\n");
			_erase_flash(0x29800);
		}
	}
	vTaskDelay(100);
	Reset();
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

	if (!strcmp(topic + DEVICE_ID_LENGTH, MQTT_TOPIC_UPGRADE)) {
		vTaskSuspend(hModbusTask);
		do_upgrade((char*)payload, length);
		vTaskResume(hModbusTask);
		return;
	}

	UARTWrite(1, "Unknown topic!\r\n");
}

static void led_timer_init()
{
	T3CON = 0;  //turn off timer
	T3CONbits.TCKPS = 3; //clock divider=256
	PR3 = 62500UL / 10; // period=0.1 sec
	TMR3 = 0; // init timer counter value
	IFS0bits.T3IF = 0; //interrupt flag off
	IEC0bits.T3IE = 1; //interrupt activated
	T3CONbits.TON = 1; // timer start
}

static int gprs_rssi = 30;
static int status_cnt = 0;
unsigned int gprs_data = 0;
unsigned int rs485_data = 0;

void __attribute__ ((interrupt,no_auto_psv)) _T3Interrupt (void)
{
	TMR3 = 0; // counter reinitialized

	if (gsmDebugOn) {
		if (status_cnt == 0)
			IOPut(p18, on);
		if (status_cnt++ >= gprs_rssi) {
			IOPut(p18, off);
			status_cnt = 0;
		}
	}

	if (gprs_data > 0) {
		IOPut(p20, toggle);
		if (gprs_data > 50)
			gprs_data = 50;
		if (--gprs_data == 0)
			IOPut(p20, off);
	}

	if (rs485_data > 0) {
		IOPut(p21, toggle);
		if (rs485_data > 50)
			rs485_data = 50;
		if (--rs485_data == 0)
			IOPut(p21, off);
	}
	
	IFS0bits.T3IF = 0; // clear interrupt flag
}

void FlyportTask()
{
	init = 0;
	int connected = 0;
	unsigned long rssi_lastime = 0;
	unsigned long ad_lastime = 0;
	char ad_info[80];
	msg_hdr_t *msg = (msg_hdr_t *)(mqtt.buffer + 50);

	SPIFlashInit();
	led_timer_init();
	
	// Initialize the RS485
	RS485Off(port485);
	RS485Remap(port485, TX_485, RX_485, DE_485, RE_485);
	
	vTaskDelay(20);
    UARTWrite(1,"MQTT Task Started...\r\n");

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
		IOPut(p18, toggle);
		IOPut(p20, toggle);
		IOPut(p19, toggle);
		IOPut(p21, toggle);
    }
	
	IOPut(p18, off);
	IOPut(p20, off);
	IOPut(p19, off);
	IOPut(p21, off);
	vTaskDelay(20);
    UARTWrite(1,"Registered on network!\r\n");

	sprintf(ad_info, "[gw]\nmft=GeeLink\nmdl=GPRS-MODBUS\nsn=%s\nhw=2.0\nsw=1.2", GSMGetIMEI());

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
			UARTWrite(1,"Subscribed mqtt topics:\r\n");
			UARTWrite(1, topic);
			UARTWrite(1,"\r\n");
			sprintf(topic, "%s%s", devid, MQTT_TOPIC_CMD_REQ);
			MQTTClient_subscribe(&mqtt, topic);
			UARTWrite(1, topic);
			UARTWrite(1,"\r\n");
			sprintf(topic, "%s%s", devid, MQTT_TOPIC_UPGRADE);
			MQTTClient_subscribe(&mqtt, topic);
			UARTWrite(1, topic);
			UARTWrite(1,"\r\n");
		}
		else if (!init) {
			if (tickGetSeconds() > (ad_lastime + 30)) {
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

		if (connected && tickGetSeconds() > (rssi_lastime + 30)) {
			GSMSignal();
			while(LastExecStat() == OP_EXECUTION)
				vTaskDelay(1);
			char rssi[12];
			gprs_rssi = GSMGetRSSI();
			sprintf(rssi, "RSSI:%d\r\n", gprs_rssi);
			UARTWrite(1, rssi);	
			rssi_lastime = tickGetSeconds();
		}
	}
}


