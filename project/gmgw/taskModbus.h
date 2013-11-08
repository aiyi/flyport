#ifndef TASK_MODEBUS_H
#define TASK_MODEBUS_H


#define MAX_NUM_SLAVES 16
#define MAX_NUM_POLL_TASKS 10

enum {
	MSG_FEED,
	MSG_CMD_REQ,
	MSG_CMD_RSP,
};

typedef struct msg_hdr {
	unsigned short msg_type;
	unsigned short data_len;
	union {
		unsigned short feedid;
		unsigned char seqno[2];
	};
} msg_hdr_t;

typedef struct poll_cfg {
	unsigned short feedId;
	unsigned char funCode;
	unsigned short regStart;
	unsigned short nRegs;
	unsigned short period;
	unsigned long lasttime;
} poll_cfg_t;

typedef struct sys_config {
	unsigned char mode;
	unsigned char port;
	unsigned int baudrate;
	unsigned char dataBits;
	unsigned char parity;
	unsigned char stopBits;
	unsigned char slave[MAX_NUM_SLAVES];
	unsigned char nSlaves;
	poll_cfg_t pollTask[MAX_NUM_POLL_TASKS];
	unsigned char nTasks;
} sys_config_t;

extern void TaskModbus();

#endif

