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
  hat->stringFlagStart = 0;
  hat->stringFlagEnd = 0;
  hat->availableBitEvent = 5;

  if (config)
    memcpy(&hat->config, config, sizeof(AT_Config_t));
  return AT_OK;
}

void AT_Process(AT_HandlerTypeDef *hat)
{
  uint16_t          readLen, i;
  AT_EventHandler_t *handlers;
  uint8_t           isHandlerFound;
  const char        *respText;
  uint8_t           respListSize;
  uint8_t           respNb;
  AT_Data_t         *respDataPtr;
  uint8_t           bytesFlagLen;
  uint16_t          nextBufferRespStart = 0;
  uint16_t          nextBufferRespLen;

  while (1) {
    if (nextBufferRespStart) {
      hat->bufferRespLen = 0;
      for (i = nextBufferRespStart; i < nextBufferRespStart+nextBufferRespLen; i++) {
        hat->bufferResp[hat->bufferRespLen] = hat->bufferResp[i];
        hat->bufferRespLen++;
      }
      nextBufferRespStart = 0;
    }
    if (hat->stringFlagStart != 0) {
      bytesFlagLen = strlen(hat->stringFlagStart);

      if (hat->bufferRespLen < bytesFlagLen) {
        readLen = hat->serial.read(&hat->bufferResp[hat->bufferRespLen], bytesFlagLen-hat->bufferRespLen);
        if (readLen <= 0) continue;
        hat->bufferRespLen += readLen;
      }

      if (hat->bufferRespLen >= bytesFlagLen) {
        if (strncmp((const char*)hat->bufferResp, hat->stringFlagStart, bytesFlagLen) == 0) {
          hat->stringFlagStart = 0;
          hat->rtos.eventSet(AT_EVT_BYTES_FLAG_START);
          hat->bufferRespLen = 0;
          continue;
        }
      }
    }

    // is any line in middle of buffer
    if (hat->stringFlagStart != 0 && hat->bufferRespLen > 2) {
      for (i = 1; i < hat->bufferRespLen-1; i++) {
        if (hat->bufferResp[i-1] == '\r' && hat->bufferResp[i] == '\n') {
          nextBufferRespLen = hat->bufferRespLen - (i+1);
          nextBufferRespStart = hat->bufferRespLen = i+1;
          break;
        }
      }
    }

    // search "\r\n"
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
      goto next;
    } else if (strncmp((const char*)hat->bufferResp, "OK", 2) == 0) {
      hat->rtos.eventSet(AT_EVT_OK);
    } else if (strncmp((const char*)hat->bufferResp, "ERROR", 5) == 0) {
      hat->rtos.eventSet(AT_EVT_ERROR);
    } else if (strncmp((const char*)hat->bufferResp, "+CME ERROR", 10) == 0) {
      hat->rtos.eventSet(AT_EVT_ERROR);
    } else if (hat->stringFlagEnd != 0
                && strncmp((const char*)hat->bufferResp,
                           hat->stringFlagEnd,
                           strlen(hat->stringFlagEnd)) == 0)
    {
      hat->rtos.eventSet(AT_EVT_BYTES_FLAG_END);
      hat->stringFlagEnd = 0;
      if (hat->currentCommand.resp != 0 && hat->currentCommand.resp->ptr != 0) {
        memcpy(hat->currentCommand.resp->ptr,
               hat->bufferResp,
               (hat->currentCommand.resp->size < hat->bufferRespLen)?
                   hat->currentCommand.resp->size : hat->bufferRespLen);
      }
    } else {
      // check whether command waiting response
      if (hat->currentCommand.cmd != 0
          && hat->currentCommand.cmdLen != 0
          && strncmp((const char*)&hat->bufferResp[0],
                     hat->currentCommand.cmd,
                     hat->currentCommand.cmdLen) == 0)
      {
        respListSize  = hat->currentCommand.respListSize;
        respNb        = hat->currentCommand.respNb;
        respDataPtr   = hat->currentCommand.resp;
        respText      = (const char*) hat->bufferResp + hat->currentCommand.cmdLen;

        if (respListSize == 0) respListSize = 1;

        while (respNb > 0 && *(respText-2) != ':') {
          if (*respText == 0) break;
          respText++;
        }

        while (respDataPtr && respListSize && respNb--) {
          respText = AT_ParseResponse(respText, respDataPtr);
          respDataPtr++;
        }

        if (--respListSize) {
          hat->currentCommand.respListSize = respListSize;
          hat->currentCommand.resp = respDataPtr;
        } else {
          hat->rtos.eventSet(AT_EVT_CMD_RESP);
        }

        // if command readinto
        if (hat->currentCommand.buffer != 0 && hat->currentCommand.readLen != 0) {
          hat->serial.readinto(hat->currentCommand.buffer,
                               *(hat->currentCommand.readLen));
        }
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

          if (recv.buffer != 0) {
            hat->serial.readinto(recv.buffer, recv.readLen);
          }
        }
      }
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

/**
 * example:
 *
 * < <cmd><next string><CR><LF>
 *
 * then callback will call
 *
 * cb(<cmd><next string><CR><LF>, size)
 *
 *
 * @param hat
 * @param cmd
 * @param app
 * @param cb
 * @return
 */
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

/**
 * example:
 *
 * < <cmd>: resp, resp
 *
 * then callback will call and return buffer and length of data should be read
 *
 * < <buffer with length>
 *
 * can be used for "+RECEIVE" in simcom
 *
 * @param hat
 * @param cmd
 * @param app
 * @param respNb
 * @param respData
 * @param cb
 * @return
 */
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

  hat->stringFlagStart = str;

  if (hat->rtos.eventWait(AT_EVT_BYTES_FLAG_START, &events, hat->config.commandTimeout) != AT_OK) {
    goto endCmd;
  }
  
endCmd:
  return status;
}

/**
 * example (without param):
 * > AT+<cmd>
 * < OK
 *
 * example (with param):
 * > AT+<cmd>: param, param
 * < +<cmd>: resp, resp
 * < OK
 *
 * @param hat
 * @param cmd
 * @param paramNb
 * @param params
 * @param respNb
 * @param resp
 * @return AT_Status_t
 */
AT_Status_t AT_Command(AT_HandlerTypeDef *hat, AT_Command_t cmd, 
                       uint8_t paramNb, AT_Data_t *params, 
                       uint8_t respNb, AT_Data_t *resp)
{
  return AT_CommandWithTimeout(hat, cmd, paramNb, params, respNb, resp,
                               hat->config.commandTimeout);
}


AT_Status_t AT_CommandWithTimeout(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                                  uint8_t paramNb, AT_Data_t *params,
                                  uint8_t respNb, AT_Data_t *resp, uint32_t timeout)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen;

  if (hat->rtos.mutexLock(timeout) != AT_OK) return AT_ERROR;
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

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, timeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) {
    goto endCmd;
  }

  if (respNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, 1);
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}

/**
 * example:
 * > AT+<cmd>?
 * < +<cmd>: resp, resp
 * < OK
 *
 * @param hat
 * @param cmd
 * @param respNb
 * @param resp
 * @return AT_Status_t
 */
AT_Status_t AT_Check(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                     uint8_t respNb, AT_Data_t *resp)
{
  return AT_CheckWithMultResp(hat, cmd, 1, respNb, resp);
}

/**
 * example:
 * > AT+<cmd>?
 * < +<cmd>: resp
 * < +<cmd>: resp
 * < +<cmd>: resp
 * < OK
 *
 * @param hat
 * @param cmd
 * @param respListSize
 * @param respDataNb
 * @param resp
 * @return
 */
AT_Status_t AT_CheckWithMultResp(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                                 uint8_t respListSize, uint8_t respDataNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen = 0;

  if (hat->rtos.mutexLock(hat->config.checkTimeout) != AT_OK) return AT_ERROR;
  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = snprintf((char*)hat->bufferCmd, AT_BUF_CMD_SZ, "AT%s?\r\n", cmd);

  if (respListSize > 0 && respDataNb > 0) {
    hat->currentCommand.cmdLen        = strlen(cmd);
    hat->currentCommand.cmd           = cmd;
    hat->currentCommand.respListSize  = respListSize;
    hat->currentCommand.respNb        = respDataNb;
    hat->currentCommand.resp          = resp;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  uint32_t events;

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.checkTimeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) goto endCmd;

  if (respListSize > 0 && respDataNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, 1);
  }

  status = AT_OK;

endCmd:
  if (respListSize > 0 && respDataNb > 0)
    memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}


AT_Status_t AT_CommandReadInto(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                               void *buffer, uint16_t *length,
                               uint8_t paramNb, AT_Data_t *params,
                               uint8_t respNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen;

  if (hat->rtos.mutexLock(hat->config.commandTimeout) != AT_OK) return AT_ERROR;
  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen  = strlen(cmd);
    hat->currentCommand.cmd     = cmd;
    hat->currentCommand.respNb  = respNb;
    hat->currentCommand.resp    = resp;
    hat->currentCommand.buffer  = buffer;
    hat->currentCommand.readLen = length;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  uint32_t events;

  if (respNb > 0 && hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.commandTimeout) != AT_OK) {
    goto endCmd;
  }

  if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.commandTimeout) != AT_OK) {
    goto endCmd;
  }

  if (events & AT_EVT_ERROR) {
    goto endCmd;
  }

  if (respNb > 0) {
    events = 0;
    hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, 1);
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}

/**
 * example:
 *
 * > AT+<cmd>: param, param
 * < <flagStart>
 * > <data>
 * < <flagEnd>
 * < +<cmd>: resp resp
 * < OK
 *
 * @param hat
 * @param cmd
 * @param flagStart
 * @param flagEnd
 * @param data
 * @param length
 * @param paramNb
 * @param params
 * @param respNb
 * @param resp
 * @return
 */
AT_Status_t AT_CommandWrite(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                            const char *flagStart, const char *flagEnd,
                            const uint8_t *data, uint16_t length,
                            uint8_t paramNb, AT_Data_t *params,
                            uint8_t respNb, AT_Data_t *resp)
{
  AT_Status_t status = AT_ERROR;
  uint16_t writecmdLen = 0;
  uint32_t events;

  if (hat->rtos.mutexLock(hat->config.commandTimeout) != AT_OK) {
    return AT_ERROR;
  }
  hat->rtos.eventClear(AT_EVT_BYTES_FLAG_START|AT_EVT_BYTES_FLAG_END|
                       AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen  = strlen(cmd);
    hat->currentCommand.cmd     = cmd;
    hat->currentCommand.respNb  = respNb;
    hat->currentCommand.resp    = resp;
  }
  hat->stringFlagStart = flagStart;

  if (flagEnd != 0 &&  *flagEnd == 0) flagEnd = 0;
  hat->stringFlagEnd = flagEnd;

  hat->serial.write(hat->bufferCmd, writecmdLen);

  if (hat->rtos.eventWait(AT_EVT_BYTES_FLAG_START, &events, hat->config.commandTimeout) != AT_OK) {
    hat->stringFlagStart = 0;
    goto endCmd;
  }
  hat->stringFlagStart = 0;

  hat->serial.write(data, length);

  if (hat->stringFlagEnd != 0) {
    if (hat->rtos.eventWait(AT_EVT_ERROR|AT_EVT_BYTES_FLAG_END, &events, hat->config.commandTimeout) != AT_OK) {
      goto endCmd;
    }
    if (events & AT_EVT_ERROR) {
      goto endCmd;
    }
  }
  else {
    if (hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.commandTimeout) != AT_OK) {
      goto endCmd;
    }

    events = 0;
    if (respNb > 0 && hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.commandTimeout) != AT_OK) {
      goto endCmd;
    }
  }

  status = AT_OK;

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}
