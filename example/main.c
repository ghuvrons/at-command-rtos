#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <at-command.h>

AT_HandlerTypeDef AT_Handler;
AT_Data_t AT_resp[2];
uint8_t AT_resp2Buf[16];

static AT_Status_t mutexLock(uint32_t timeout);
static AT_Status_t mutexUnlock(void);
static AT_Status_t eventSet(uint32_t events);
static AT_Status_t eventWait(uint32_t events, uint32_t timeout);
static AT_Status_t eventClear(uint32_t events);

static int serialRead(uint8_t *buf, uint16_t len);
static int serialReadline(uint8_t *buf, uint16_t len);
static int serialWrite(uint8_t *buf, uint16_t len);

int main(){
  printf("Start\r\n");
  memset(&AT_Handler, 0, sizeof(AT_HandlerTypeDef));
  
  AT_Config_t config;
  config.timeout = 5000;

  AT_Handler.serial.read      = serialRead;
  AT_Handler.serial.readline  = serialReadline;
  AT_Handler.serial.write     = serialWrite;

  AT_Handler.rtos.mutexLock   = mutexLock;
  AT_Handler.rtos.mutexUnlock = mutexUnlock;
  AT_Handler.rtos.eventSet    = eventSet;
  AT_Handler.rtos.eventWait   = eventWait;
  AT_Handler.rtos.eventClear  = eventClear;

  AT_Init(&AT_Handler, &config);

  // sending command
  int g = 90;

  AT_Data_t params[2] = {
    AT_Number(g),
    AT_String("OK"),
  };

  AT_resp[1].value.string = AT_resp2Buf;

  AT_Process(&AT_Handler);
  AT_Command(&AT_Handler, "CMD", 2, params, 2, AT_resp);

  return 0;
}


static AT_Status_t mutexLock(uint32_t timeout)
{
  return AT_OK;
}


static AT_Status_t mutexUnlock(void)
{
  return AT_OK;
}


static AT_Status_t eventSet(uint32_t events)
{
  return AT_OK;
}


static AT_Status_t eventWait(uint32_t events, uint32_t timeout)
{
  return AT_OK;
}


static AT_Status_t eventClear(uint32_t events)
{
  return AT_OK;
}


static int serialRead(uint8_t *buf, uint16_t len)
{
  return 0;
}


static int serialReadline(uint8_t *buf, uint16_t len)
{
  memcpy(buf, "\r\n", 2);
  return 4;
}


static int serialWrite(uint8_t *buf, uint16_t len)
{
  return 0;
}
