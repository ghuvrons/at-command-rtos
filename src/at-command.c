#include "include/at-command.h"
#include "include/at-command/utils.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>



static int readIntoBufferResp(AT_HandlerTypeDef*, uint16_t length);

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
  memset(hat->bufferResp, 0, AT_BUF_RESP_SZ);
  hat->bufferRespLen = 0;
  hat->stringFlagStart = 0;
  hat->stringFlagEnd = 0;
  hat->availableBitEvent = 5;

  if (config)
    memcpy(&hat->config, config, sizeof(AT_Config_t));
  return AT_OK;
}

AT_Status_t AT_Start(AT_HandlerTypeDef *hat)
{
  return hat->rtos.eventSet(AT_EVT_START);
}

void AT_Process(AT_HandlerTypeDef *hat)
{
  uint16_t           readLen;
  AT_EventHandler_t  *handlers;
  uint8_t            isHandlerFound;
  const char         *respText;
  uint8_t            respNb;
  AT_Data_t          *respDataPtr;
  AT_PrefixHandler_t *prefixHandlerPtr = 0;
  const char         *stringFlagStart;
  uint8_t            stringFlagStartLen;
  uint32_t           events;

waitStarted:
  if (hat->rtos.eventWait(AT_EVT_START, &events, 2000) != AT_OK)
  {
    goto waitStarted;
  }

  while (1) {
    if (hat->bufferRespLen == 0) {
      prefixHandlerPtr  = hat->prefixhandlers;
      stringFlagStart   = hat->stringFlagStart;
      if (stringFlagStart)
        stringFlagStartLen = strlen(hat->stringFlagStart);
      else
        stringFlagStartLen = 0;
    }

    // read byte
    if ((prefixHandlerPtr != 0 && hat->bufferRespLen < prefixHandlerPtr->prefixLen) ||
        (stringFlagStart != 0 && hat->bufferRespLen < stringFlagStartLen))
    {
      readLen = readIntoBufferResp(hat, 1);
      if (readLen == 0) continue;
      else if (readLen < 0) {
        hat->bufferRespLen = 0;
        continue;
      }
    }

    // Check stringFlagStart
    if (stringFlagStart != 0) {
      if (hat->bufferRespLen == stringFlagStartLen) {
        if (strncmp((const char*)hat->bufferResp, hat->stringFlagStart, stringFlagStartLen) == 0) {
          hat->stringFlagStart = 0;
          hat->rtos.eventSet(AT_EVT_BYTES_FLAG_START);
          hat->bufferRespLen = 0;
          stringFlagStart = 0;
          stringFlagStartLen = 0;
          continue;
        }
        else {
          stringFlagStart = 0;
          stringFlagStartLen = 0;
        }
      }
    }

    // Check prefix
    if (prefixHandlerPtr != 0) {
      if (hat->bufferRespLen >= prefixHandlerPtr->prefixLen) {
        while (prefixHandlerPtr && hat->bufferRespLen >= prefixHandlerPtr->prefixLen) {
          if (strncmp((const char*)hat->bufferResp,
                      prefixHandlerPtr->prefix,
                      prefixHandlerPtr->prefixLen) == 0)
          {
            prefixHandlerPtr->callback(hat);
            hat->bufferRespLen = 0;
            continue;
          }
          prefixHandlerPtr = prefixHandlerPtr->next;
        }
      }
    }

    // check whether buffer is one line
    if (hat->bufferRespLen >= 2 &&
        (strncmp((const char*)(hat->bufferResp+hat->bufferRespLen-2), "\r\n", 2) == 0))
    {
      goto handleCommand;
    }
    else if (prefixHandlerPtr != 0 || stringFlagStart != 0) {
      continue;
    }

    // search "\r\n"
    while (hat->bufferRespLen > 0 && *(hat->bufferResp+hat->bufferRespLen-1) == '\r') {
      readLen = readIntoBufferResp(hat, 1);
      if (readLen == 0) continue;
      else if (readLen < 0) {
        goto handleCommand;
      }
    }

    // try to read one line
    if (hat->bufferRespLen < 2 
        || strncmp((const char*)(hat->bufferResp+hat->bufferRespLen-2), "\r\n", 2) != 0)
    {
      if ((AT_BUF_RESP_SZ - (int)hat->bufferRespLen - 1) < 0) {
        goto handleCommand;
      }

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

    handleCommand:
    if (hat->bufferRespLen == 2 && strncmp((const char*)hat->bufferResp, "\r\n", 2) == 0) {
      goto next;
    }
    else if (strncmp((const char*)hat->bufferResp, "OK", 2) == 0) {
      hat->rtos.eventSet(AT_EVT_OK);
    }
    else if (strncmp((const char*)hat->bufferResp, "ERROR", 5) == 0) {
      hat->rtos.eventSet(AT_EVT_ERROR);
    }
    else if (strncmp((const char*)hat->bufferResp, "+CME ERROR", 10) == 0) {
      hat->rtos.eventSet(AT_EVT_ERROR);
    }
    else if (hat->stringFlagEnd != 0
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
    }
    else {
      // check whether command waiting response
      if (hat->currentCommand.cmd != 0
          && hat->currentCommand.cmdLen != 0
          && strncmp((const char*)&hat->bufferResp[0],
                     hat->currentCommand.cmd,
                     hat->currentCommand.cmdLen) == 0)
      {
        respNb        = hat->currentCommand.respNb;
        respDataPtr   = hat->currentCommand.resp;
        respText      = (const char*) hat->bufferResp + hat->currentCommand.cmdLen;

        while (respNb > 0 && *(respText-2) != ':') {
          if (*respText == 0) break;
          respText++;
        }

        if (hat->currentCommand.respListSize > 0) {
          if (hat->currentCommand.respListType == AT_LIST_ONE_LINE) {
            AT_ParseResponseList(respText, hat->currentCommand.respListSize, respNb, respDataPtr);
            hat->currentCommand.respListSize = 1;
          } else {
            AT_ParseResponse(respText, respNb, respDataPtr);
          }
        }

        hat->currentCommand.respListSize--;

        if (hat->currentCommand.respListSize > 0) {
          respDataPtr += respNb;
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

        AT_ParseResponse(respText, respNb, respDataPtr);

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


AT_Status_t AT_OnPrefix(AT_HandlerTypeDef *hat,
                        const char *prefix,
                        void (*callback)(AT_HandlerTypeDef *atPtr))
{
  if (strlen(prefix) == 0) return AT_ERROR;

  AT_PrefixHandler_t *currentptr = hat->prefixhandlers;
  AT_PrefixHandler_t *prevPtr = 0;
  AT_PrefixHandler_t *newHandlers = (AT_PrefixHandler_t*) malloc(sizeof(AT_PrefixHandler_t));

  newHandlers->prefix = prefix;
  newHandlers->prefixLen = strlen(prefix);
  newHandlers->callback = (void (*)(void *)) callback;
  newHandlers->next = 0;

  if (hat->prefixhandlers == 0) {
    hat->prefixhandlers = newHandlers;
    return AT_OK;
  }

  // Add new handler in link list sort by prefixLen
  currentptr = hat->prefixhandlers;
  while (currentptr) {
    if (strcmp(currentptr->prefix, newHandlers->prefix) == 0) {
      currentptr->callback = newHandlers->callback;
      free(newHandlers);
      break;
    }
    if (currentptr->prefixLen > newHandlers->prefixLen) {
      newHandlers->next = currentptr;
      if (prevPtr == 0) {
        hat->prefixhandlers = newHandlers;
      }
      else {
        prevPtr->next = newHandlers;
      }
      break;
    }
    prevPtr = currentptr;
    currentptr = currentptr->next;
    if (currentptr == 0) {
      prevPtr->next = newHandlers;
      break;
    }
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
  uint32_t events;

  hat->stringFlagStart = str;

  return hat->rtos.eventWait(AT_EVT_BYTES_FLAG_START, &events, hat->config.commandTimeout);
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
  AT_Status_t status;
  uint16_t writecmdLen;
  uint32_t events;

  status = hat->rtos.mutexLock(timeout);
  if (status != AT_OK) return status;

  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (strncmp(cmd, "+COPS", 5) == 0) {
    status = AT_ERROR;
  }
  if (respNb > 0) {
    hat->currentCommand.cmdLen        = strlen(cmd);
    hat->currentCommand.cmd           = cmd;
    hat->currentCommand.respListSize  = 1;
    hat->currentCommand.respNb        = respNb;
    hat->currentCommand.resp          = resp;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  if (strncmp(cmd, "+COPS", 5) == 0) {
    status = AT_ERROR;
  }
  // wait response
  status = hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, timeout);
  if (status == AT_OK){
    if (events & AT_EVT_ERROR) {
      status = AT_ERROR;
    }
  }
  else if (status == AT_TIMEOUT) {
    status = AT_RESPONSE_TIMEOUT;
  }

  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}


AT_Status_t AT_TestWithTimeout(AT_HandlerTypeDef *hat, AT_Command_t cmd,
                               uint8_t respListSize, uint8_t respNb, AT_Data_t *resp,
                               uint32_t timeout)
{
  AT_Status_t status;
  uint16_t writecmdLen;
  uint32_t events;

  status = hat->rtos.mutexLock(timeout);
  if (status != AT_OK) return status;

  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR);

  writecmdLen = snprintf((char*)hat->bufferCmd, AT_BUF_CMD_SZ, "AT%s=?\r\n", cmd);

  if (respNb > 0) {
    hat->currentCommand.cmdLen        = strlen(cmd);
    hat->currentCommand.cmd           = cmd;
    hat->currentCommand.respListType  = AT_LIST_ONE_LINE;
    hat->currentCommand.respListSize  = 1;
    hat->currentCommand.respNb        = respNb;
    hat->currentCommand.resp          = resp;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  status = hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, timeout);
  if (status == AT_OK){
    if (events & AT_EVT_ERROR) {
      status = AT_ERROR;
    }
  }
  else if (status == AT_TIMEOUT) {
    status = AT_RESPONSE_TIMEOUT;
  }

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
  AT_Status_t status;
  uint16_t writecmdLen;
  uint32_t events;

  status = hat->rtos.mutexLock(hat->config.checkTimeout);
  if (status != AT_OK) return status;

  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR);

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
  status = hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.checkTimeout);
  if (status == AT_OK){
    if (events & AT_EVT_ERROR) {
      status = AT_ERROR;
    }
  }
  else if (status == AT_TIMEOUT) {
    status = AT_RESPONSE_TIMEOUT;
  }

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
  AT_Status_t status;
  uint16_t writecmdLen;
  uint32_t events;

  status = hat->rtos.mutexLock(hat->config.commandTimeout);
  if (status != AT_OK) return status;

  hat->rtos.eventClear(AT_EVT_OK|AT_EVT_ERROR|AT_EVT_CMD_RESP);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen        = strlen(cmd);
    hat->currentCommand.cmd           = cmd;
    hat->currentCommand.respListSize  = 1;
    hat->currentCommand.respNb        = respNb;
    hat->currentCommand.resp          = resp;
    hat->currentCommand.buffer        = buffer;
    hat->currentCommand.readLen       = length;
  }

  hat->serial.write(hat->bufferCmd, writecmdLen);

  // wait response
  if (respNb > 0) {
    status = hat->rtos.eventWait(AT_EVT_CMD_RESP, &events, hat->config.commandTimeout);
    if (status != AT_OK) {
      if (status == AT_TIMEOUT) {
        status = AT_RESPONSE_TIMEOUT;
      }
      goto endCmd;
    }
  }

  status = hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.commandTimeout);
  if (status == AT_OK){
    if (events & AT_EVT_ERROR) {
      status = AT_ERROR;
    }
  }
  else if (status == AT_TIMEOUT) {
    status = AT_RESPONSE_TIMEOUT;
  }

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
  AT_Status_t status;
  uint16_t writecmdLen;
  uint32_t events;

  status = hat->rtos.mutexLock(hat->config.commandTimeout);
  if (status != AT_OK) return status;

  hat->rtos.eventClear(AT_EVT_BYTES_FLAG_START|AT_EVT_BYTES_FLAG_END|
                       AT_EVT_OK|AT_EVT_ERROR);

  writecmdLen = AT_WriteCommand(hat->bufferCmd, AT_BUF_CMD_SZ, cmd, paramNb, params);

  if (respNb > 0) {
    hat->currentCommand.cmdLen        = strlen(cmd);
    hat->currentCommand.cmd           = cmd;
    hat->currentCommand.respListSize  = 1;
    hat->currentCommand.respNb        = respNb;
    hat->currentCommand.resp          = resp;
  }
  hat->stringFlagStart = flagStart;

  if (flagEnd != 0 && *flagEnd == 0) flagEnd = 0;
  hat->stringFlagEnd = flagEnd;

  hat->serial.write(hat->bufferCmd, writecmdLen);

  status = hat->rtos.eventWait(AT_EVT_BYTES_FLAG_START, &events, hat->config.commandTimeout);
  if (status != AT_OK) {
    hat->stringFlagStart = 0;
    if (status == AT_TIMEOUT) {
      status = AT_RESPONSE_TIMEOUT;
    }
    goto endCmd;
  }
  hat->stringFlagStart = 0;

  hat->serial.write(data, length);
  if (hat->stringFlagEnd != 0) {
    status = hat->rtos.eventWait(AT_EVT_ERROR|AT_EVT_BYTES_FLAG_END, &events, hat->config.commandTimeout);
    if (status == AT_OK){
      if (events & AT_EVT_ERROR) {
        status = AT_ERROR;
      }
    }
    else if (status == AT_TIMEOUT) {
      status = AT_RESPONSE_TIMEOUT;
    }
  }
  else {
    status = hat->rtos.eventWait(AT_EVT_OK|AT_EVT_ERROR, &events, hat->config.commandTimeout);
    if (status == AT_OK){
      if (events & AT_EVT_ERROR) {
        status = AT_ERROR;
      }
    }
    else if (status == AT_TIMEOUT) {
      status = AT_RESPONSE_TIMEOUT;
    }
  }

endCmd:
  if (respNb > 0) memset(&hat->currentCommand, 0, sizeof(hat->currentCommand));
  hat->rtos.mutexUnlock();
  return status;
}


static int readIntoBufferResp(AT_HandlerTypeDef *hat, uint16_t length)
{
  if ((hat->bufferRespLen + length + 1) > AT_BUF_RESP_SZ) {
    return -1;
  }

  int readLen = hat->serial.read(&hat->bufferResp[hat->bufferRespLen], length);
  if (readLen <= 0) return 0;
  hat->bufferRespLen += readLen;
  return hat->bufferRespLen;
}

