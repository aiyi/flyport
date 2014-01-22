#include "taskFlyport.h"
#include "taskModbus.h"
#include "MQTTClient.h"
#include "mb.h"

extern xQueueHandle xQueueModbus;
extern xQueueHandle xQueueMqtt;

extern volatile UCHAR ucMBBuf[EXTRA_HEAD_ROOM + MB_SER_PDU_SIZE_MAX];

extern sys_config_t config; 
extern int init;

#define Swap2Bytes(val) \
	( (((val) >> 8) & 0x00FF) | (((val) << 8) & 0xFF00) )
#define Swap4Bytes(val) \
	( (((val) >> 24) & 0x000000FF) | (((val) >>  8) & 0x0000FF00) | \
	(((val) <<	8) & 0x00FF0000) | (((val) << 24) & 0xFF000000) )

static void send_msg(unsigned type, UCHAR *data, unsigned len)
{
	msg_hdr_t *pMsg = (msg_hdr_t *)(data - 4);
	pMsg->msg_type = type;
	pMsg->data_len = len;
	xQueueSend(xQueueMqtt, pMsg, portMAX_DELAY);
}

static eMBErrorCode poll_slave(UCHAR slaveid, poll_cfg_t *task)
{
	eMBErrorCode eStatus;
	UCHAR *ucRcvFrame;
	USHORT usLength;

	UARTWrite(1, "Modbus polling...\r\n");
	eStatus = eMBMReadRegisters(slaveid, task->funCode, task->regStart, task->nRegs, &ucRcvFrame, &usLength);
	
	if(eStatus == MB_ENOERR || eStatus == MB_ENOREG) {
		UCHAR *data = ucRcvFrame - 2;
		*((unsigned short *)data) = Swap2Bytes(task->feedId);
		UARTWrite(1, "Replied\r\n");
		send_msg(MSG_FEED, data, 2 + usLength);
	}
	else {
		char errmsg[25];
		sprintf(errmsg, "slave id=%u err=%d\r\n", slaveid, eStatus);
		UARTWrite(1, errmsg);
	}
	return eStatus;
}

static void do_poll()
{
	poll_cfg_t *task;
	int i, j;

	for (i = 0; i < config.nTasks; i++) {
		task = &config.pollTask[i];
		unsigned long now = tickGetSeconds();
		if (now - task->lasttime < task->period)
			continue;
		task->lasttime = now;
		for (j = 0; j < config.nSlaves; j++) {
			poll_slave(config.slave[j], task);
		}
	}
}

void TaskModbus()
{	
	vTaskDelay(20);
    UARTWrite(1,"Modbus Task Started...\r\n");

	msg_hdr_t *pMsg = (msg_hdr_t *)(ucMBBuf + EXTRA_HEAD_ROOM - sizeof(msg_hdr_t));

	while (1) {
		if (xQueueReceive(xQueueModbus, (void *)pMsg, 0) && init) {
			eMBErrorCode eStatus;
			UCHAR *ucRcvFrame;
			USHORT usLength;
			UCHAR *data = (UCHAR*)pMsg + sizeof(msg_hdr_t);
			UCHAR ucSlave = *data;
			UCHAR ucfunc = *(data + 1);
			
			UARTWrite(1, "Modbus request...\r\n");
			eStatus = eMBMSendData(data, pMsg->data_len - 2, &ucRcvFrame, &usLength);
			
			data = ucRcvFrame - 2;
			data[0] = pMsg->seqno[0];
			data[1] = pMsg->seqno[1];
			if (eStatus != MB_ENOERR) {
				usLength = 3;
				data[2] = ucSlave;
				data[3] = ( UCHAR )( ucfunc | MB_FUNC_ERROR );
				if (eStatus == MB_ETIMEDOUT)
					data[4] = MB_EX_SLAVE_BUSY;
				else
					data[4] = MB_EX_NONE;
				
				char strStatus[15];
				sprintf(strStatus, "eStatus=%d\r\n", eStatus);
				UARTWrite(1, strStatus);
			}
			else {
				UARTWrite(1, "Replied\r\n");
			}
			send_msg(MSG_CMD_RSP, data, 2 + usLength);
		}

		if (init) {
			do_poll();
		}

		vTaskDelay(1);
	}
}


