#ifndef AT_COMMAND_H
#define AT_COMMAND_H

#include "at-command/types.h"
#include "at-command/config.h"

typedef struct {
  uint32_t commandTimeout;
  uint32_t checkTimeout;
} AT_Config_t;

typedef struct {
  // configs
  AT_Config_t config;

  struct {
    int (*read)(uint8_t *dst, uint16_t len);
    int (*readline)(uint8_t *dst, uint16_t len);
    int (*readinto)(void *buf, uint16_t len);
    int (*write)(const uint8_t *src, uint16_t len);
  } serial;

  struct {
    AT_Status_t (*mutexLock)(uint32_t timeout);
    AT_Status_t (*mutexUnlock)(void);
    AT_Status_t (*eventSet)(uint32_t events);
    AT_Status_t (*eventWait)(uint32_t events, uint32_t *onEvents, uint32_t timeout);
    AT_Status_t (*eventClear)(uint32_t events);
  } rtos;

  // at process
  AT_EventHandler_t *handlers;
  AT_PrefixHandler_t *prefixhandlers;
  AT_CmdResp_t currentCommand;
  const char *stringFlagStart;
  const char *stringFlagEnd;
  uint32_t availableBitEvent;
  struct AT_BufferReadTo *bufferReadTo;

  // buffers
  uint8_t bufferCmd[AT_BUF_CMD_SZ];
  uint16_t bufferRespLen;
  uint8_t bufferResp[AT_BUF_RESP_SZ];
} AT_HandlerTypeDef;

AT_Status_t AT_Init(AT_HandlerTypeDef*, AT_Config_t*);
void AT_Process(AT_HandlerTypeDef*);

AT_Status_t AT_On(AT_HandlerTypeDef*, AT_Command_t, void *app,
                  uint8_t respNb, AT_Data_t *,
                  AT_EH_Callback_t);

AT_Status_t AT_OnPrefix(AT_HandlerTypeDef*,
                        const char *prefix,
                        void (*cb)(AT_HandlerTypeDef *atPtr));

AT_Status_t AT_ReadlineOn(AT_HandlerTypeDef*, AT_Command_t, void *app, AT_EH_CallbackReadline_t);

AT_Status_t AT_ReadIntoBufferOn(AT_HandlerTypeDef*, AT_Command_t, void *app,
                                uint8_t respNb, AT_Data_t *,
                                AT_EH_CallbackBufReadTo_t);

AT_Status_t AT_WaitStringFlag(AT_HandlerTypeDef*, const char *str, uint8_t len);

AT_Status_t AT_Command(AT_HandlerTypeDef*, AT_Command_t, 
                       uint8_t paramNb, AT_Data_t *params, 
                       uint8_t respNb, AT_Data_t *resp);

AT_Status_t AT_CommandWithTimeout(AT_HandlerTypeDef*, AT_Command_t,
                                  uint8_t paramNb, AT_Data_t *params,
                                  uint8_t respNb, AT_Data_t *resp, uint32_t timeout);

AT_Status_t AT_TestWithTimeout(AT_HandlerTypeDef*, AT_Command_t,
                               uint8_t respListSize, uint8_t respNb, AT_Data_t *resp,
                               uint32_t timeout);

AT_Status_t AT_Check(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                     uint8_t respNb, AT_Data_t *resp);

AT_Status_t AT_CheckWithMultResp(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                                 uint8_t respListSize, uint8_t respDataNb, AT_Data_t *resp);

AT_Status_t AT_CommandReadInto(AT_HandlerTypeDef*, AT_Command_t,
                               void *buffer, uint16_t *length,
                               uint8_t paramNb, AT_Data_t *params,
                               uint8_t respNb, AT_Data_t *resp);

AT_Status_t AT_CommandWrite(AT_HandlerTypeDef*, AT_Command_t,
                            const char *flagStart, const char *flagEnd,
                            const uint8_t *data, uint16_t length,
                            uint8_t paramNb, AT_Data_t *params,
                            uint8_t respNb, AT_Data_t *resp);
#endif
