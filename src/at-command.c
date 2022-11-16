#include "include/at-command.h"
#include "include/at-command/utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <utils/debugger.h>


AT_Status_t AT_Init(AT_HandlerTypeDef *hat, AT_Config_t *config)
{
  if (hat->serial.read == 0)      return AT_ERROR;
  if (hat->serial.readline == 0)  return AT_ERROR;
  if (hat->serial.write == 0)     return AT_ERROR;

  if (hat->rtos.mutexLock == 0)   return AT_ERROR;
  if (hat->rtos.mutexUnlock == 0) return AT_ERROR;
  if (hat->rtos.eventSet == 0)    return AT_ERROR;
  if (hat->rtos.eventWait == 0)   return AT_ERROR;
  if (hat->rtos.eventClear == 0)  return AT_ERROR;

  memset(hat->bufferCmd, 0, AT_BUF_CMD_SZ);
  hat->bufferRespLen = 0;
  hat->stringFlag = 0;
  hat->availableBitEvent = 5;

  if (config)
    memcpy(&hat->config, config, sizeof(AT_Config_t));
  return AT_OK;
}

void AT_Process(AT_HandlerTypeDef *hat)
{
  uint16_t          readLen;
  AT_EventHandler_t *handlers;
  uint8_t           isHandlerFound;
  const char        *respText;
  uint8_t           respNb;
  AT_Data_t         *respDataPtr;
  uint8_t           bytesFlagLen;

  while (1) {
    if (hat->stringFlag != 0) {
      bytesFlagLen = strlen(hat->stringFlag);

      if (hat->bufferRespLen < bytesFlagLen) {
        readLen = hat->serial.read(&hat->bufferResp[hat->bufferRespLen], bytesFlagLen-hat->bufferRespLen);
        if (readLen <= 0) continue;
        hat->bufferRespLen += readLen;
      }

      if (hat->bufferRespLen >= bytesFlagLen) {
        if (strncmp((const char*)hat->bufferResp, hat->stringFlag, bytesFlagLen) == 0) {
          hat->stringFlag = 0;
          hat->rtos.eventSet(AT_EVT_BYTES_FLAG);
          hat->bufferRespLen = 0;
          continue;
        }
      }
    }

    while (hat->bufferRespLen > 0 && *(hat->bufferResp+hat->bufferRespLen-1) == '\r') {
      readLen = hat->serial.read(&hat->bufferResp[hat->bufferRespLen], 1);
      if (readLen <= 0) continue;
      hat->bufferRespLen += readLen;
    }

    // try to read one line
    if (hat->bufferRespLen < 2 
        || strncmp((const char*)(hat->bufferResp+hat->bufferRespLen-2), "\r\n", 2) != 0)
    {
      readLen = hat->serial.readline(&hat->bufferResp[hat->bufferRespLen], AT_BUF_RESP_SZ-hat->bufferRespLen);
      if (readLen <= 0) continue;
      hat->bufferRespLen += readLen;

      // "continue;" if not get one line
      if (hat->bufferRespLen < 2
          || strncmp((const char*)(hat->bufferResp+hat->bufferRespLen-2), "\r\n", 2) != 0)
      {
        continue;
      }
    }

    if (hat->bufferRespLen == 2 && strncmp((const char*)hat->bufferResp, "\r\n", 2) == 0) {
    } else if (strncmp((const char*)hat->bufferResp, "OK", 2) == 0) {
      hat->rtos.eventSet(AT_EVT_OK);
    } else if (strncmp((const char*)hat->bufferResp, "ERROR", 5) == 0) {
      hat->rtos.eventSet(AT_EVT_ERROR);
    } else {
      // check whether command waiting response
      if (hat->currentCommand.cmd != 0 
          && hat->currentCommand.cmdLen != 0
          && strncmp((const char*)&hat->bufferResp[0],
                     hat->currentCommand.cmd, 
                     hat->currentCommand.cmdLen) == 0)
      {
        respNb = hat->currentCommand.respNb;
        respDataPtr = hat->currentCommand.resp;
        respText = (const char*) hat->bufferResp + hat->currentCommand.cmdLen;

        while (respNb > 0 && *(respText-2) != ':') {
          if (*respText == 0) break;
          respText++;
        }

        while (respDataPtr && respNb--) {
          respText = AT_ParseResponse(respText, respDataPtr);
          respDataPtr++;
        }

        hat->rtos.eventSet(AT_EVT_CMD_RESP);
        goto next;
      }

      // search else handlers
      isHandlerFound = 0;
      handlers = hat->handlers;

      while (handlers) {
        if (strncmp((const char*)&hat->bufferResp[0], handlers->cmdResp.cmd, handlers->cmdResp.cmdLen) == 0) {
          isHandlerFound = 1;
          break;
        }
        handlers = handlers->next;
      }

      if (isHandlerFound) {
        respNb = handlers->cmdResp.respNb;
        respDataPtr = handlers->cmdResp.resp;
        respText = (const char*)hat->bufferResp + handlers->cmdResp.cmdLen;

        while (respNb > 0 && (*(respText-2) != ':' && *(respText-1) != ',')) {
          if (*respText == 0) break;
          respText++;
        }

        while (respDataPtr && respNb--) {
          respText = AT_ParseResponse(respText, respDataPtr);
          respDataPtr++;
        }

        if (handlers->callback != 0)
          handlers->callback(handlers->app, handlers->cmdResp.resp);
        else if (handlers->callbackReadline != 0) {
          handlers->callbackReadline(handlers->app, hat->bufferResp, hat->bufferRespLen);
        }
        else if (handlers->callbackBufferReadTo != 0) {
          struct AT_BufferReadTo recv = handlers->callbackBufferReadTo(handlers->app,
                                                                       handlers->cmdResp.resp);
          hat->serial.readinto(recv.buffer, recv.length);
        }
      }

      // flush buffer resp
    }

  next:
    hat->bufferRespLen = 0;
  }

  hat->rtos.eventSet(AT_EVT_OK|AT_EVT_CMD_RESP);
}


AT_Status_t AT_On(AT_HandlerTypeDef *hat, AT_Command_t cmd, void *app,
                  uint8_t respNb, AT_Data_t *respData,
                  AT_EH_Callback_t cb)
{
  AT_EventHandler_t *handlerPtr;
  AT_EventHandler_t *newHandlers = (AT_EventHandler_t*) malloc(sizeof(AT_EventHandler_t));

  newHandlers->app = app;
  newHandlers->cmdResp.cmdLen = strlen(cmd);
  newHandlers->cmdResp.cmd = cmd;
  newHandlers->cmdResp.respNb = respNb;
  newHandlers->cmdResp.resp = respData;
  newHandlers->callback = cb;
  newHandlers->callbackReadline = 0;
  newHandlers->callbackBufferReadTo = 0;
  newHandlers->next = 0;

  if (hat->handlers == 0) {
    hat->handlers = newHandlers;
  } else {
    handlerPtr = hat->handlers;
    while (handlerPtr->next != 0) {
      handlerPtr = handlerPtr->next;
    }
    handlerPtr->next = newHandlers;
  }

  return AT_OK;
}


AT_Status_t AT_ReadlineOn(AT_HandlerTypeDef *hat, AT_Command_t cmd, void *app, AT_EH_CallbackReadline_t cb)
{
  AT_EventHandler_t *handlerPtr;
  AT_EventHandler_t *newHandlers = (AT_EventHandler_t*) malloc(sizeof(AT_EventHandler_t));

  newHandlers->app = app;
  newHandlers->cmdResp.cmdLen = strlen(cmd);
  newHandlers->cmdResp.cmd = cmd;
  newHandlers->cmdResp.respNb = 0;
  newHandlers->cmdResp.resp = 0;
  newHandlers->callback = 0;
  newHandlers->callbackReadline = cb;
  newHandlers->callbackBufferReadTo = 0;
  newHandlers->next = 0;

  if (hat->handlers == 0) {
    hat->handlers = newHandlers;
  } else {
    handlerPtr = hat->handlers;
    while (handlerPtr->next != 0) {
      handlerPtr = handlerPtr->next;
    }
    handlerPtr->next = newHandlers;
  }

  return AT_OK;
}


AT_Status_t AT_ReadIntoBufferOn(AT_HandlerTypeDef *hat, AT_Command_t cmd, void *app,
                                uint8_t respNb, AT_Data_t *respData,
                                AT_EH_CallbackBufReadTo_t cb)
{
  AT_EventHandler_t *handlerPtr;
  AT_EventHandler_t *newHandlers = (AT_EventHandler_t*) malloc(sizeof(AT_EventHandler_t));

  newHandlers->app = app;
  newHandlers->cmdResp.cmdLen = strlen(cmd);
  newHandlers->cmdResp.cmd = cmd;
  newHandlers->cmdResp.respNb = respNb;
  newHandlers->cmdResp.resp = respData;
  newHandlers->callback = 0;
  newHandlers->callbackReadline = 0;
  newHandlers->callbackBufferReadTo = cb;
  newHandlers->next = 0;

  if (hat->handlers == 0) {
    hat->handlers = newHandlers;
  } else {
    handlerPtr = hat->handlers;
    while (handlerPtr->next != 0) {
      handlerPtr = handlerPtr->next;
    }
    handlerPtr->next = newHandlers;
  }

  return AT_OK;
}


AT_Status_t AT_WaitStringFlag(AT_HandlerTypeDef *hat, const char *str, uint8_t len)
{
  AT_Status_t status = AT_OK;
  uint32_t events;

  hat->stringFlag = str;

  if (hat->rtos.eventWait(AT_EVT_BYTES_FLAG, &events, hat->config.timeout) != AT_OK) {
    goto endCmd;
  }
  
endCmd:
  return status;
}


AT_Status_t AT_Command(AT_HandlerTypeDef *hat, AT_Command_t cmd, 
                       uint8_t paramNb, AT_Data_t *params, 
                       uint8_t respNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen;

  if (hat->rtos.mutexLock(hat->config.timeout) != AT_OK) return AT_ERROR;
  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen  = strlen(cmd);
    hat->currentCommand.cmd     = cmd;
    hat->currentCommand.respNb  = respNb;
    hat->currentCommand.resp    = resp;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  uint32_t events;

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.timeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) goto endCmd;

  if (respNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.timeout);
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}

AT_Status_t AT_Check(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                     uint8_t respNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen = 0;

  if (hat->rtos.mutexLock(hat->config.timeout) != AT_OK) return AT_ERROR;
  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = snprintf((char*)hat->bufferCmd, AT_BUF_CMD_SZ, "AT%s?\r\n", cmd);

  if (respNb > 0) {
    hat->currentCommand.cmdLen  = strlen(cmd);
    hat->currentCommand.cmd     = cmd;
    hat->currentCommand.respNb  = respNb;
    hat->currentCommand.resp    = resp;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  uint32_t events;

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.timeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) goto endCmd;

  if (respNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.timeout);
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}


AT_Status_t AT_CommandWrite(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                            const char *flag,
                            uint8_t *data, uint16_t length,
                            uint8_t paramNb, AT_Data_t *params,
                            uint8_t respNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen = 0;
  uint32_t events;

  if (hat->rtos.mutexLock(hat->config.timeout) != AT_OK) {
    return AT_ERROR;
  }
  hat->rtos.eventClear(AT_EVT_BYTES_FLAG|AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen  = strlen(cmd);
    hat->currentCommand.cmd     = cmd;
    hat->currentCommand.respNb  = respNb;
    hat->currentCommand.resp    = resp;
  }
  hat->stringFlag = flag;

  hat->serial.write(hat->bufferCmd, writecmdLen);

  if (hat->rtos.eventWait(AT_EVT_BYTES_FLAG, &events, hat->config.timeout) != AT_OK) {
    hat->stringFlag = 0;
    goto endCmd;
  }
  hat->stringFlag = 0;

  hat->serial.write(data, length);

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.timeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) {
    goto endCmd;
  }

  if (respNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.timeout);
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}
