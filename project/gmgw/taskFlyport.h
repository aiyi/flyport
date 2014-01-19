#include "HWlib.h"
#include "INTlib.h"
#include "string.h"
#include "RTCClib.h"
#include "SPIFlash.h"
#include "HILOlib.h"
#include "SMSlib.h"
#include "CALLlib.h"
#include "LowLevelLib.h"
#include "DATAlib.h"
#include "TCPlib.h"
#include "HTTPlib.h"
#include "SMTPlib.h"
#include "FTPlib.h"
#include "FSlib.h"
#include "libpic30.h"

//	RTOS components - Semaphore and queues
extern xQueueHandle xQueue;
extern xSemaphoreHandle xSemFrontEnd;
extern xSemaphoreHandle xSemHW;

//	FrontEnd variables
extern int xFrontEndStat;
extern int xErr;

void FlyportTask();

