#include "taskFlyport.h"

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

    while(1)
    {
		
	}
}
