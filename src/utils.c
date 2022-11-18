#include "at-command/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


uint16_t AT_WriteCommand(uint8_t *buffer, uint16_t bufferSz, 
                         AT_Command_t cmd, uint8_t paramNb, AT_Data_t *params)
{
  uint16_t writeLen = 0;
  uint16_t tmpWriteLen = 0;

  memcpy(buffer, "AT", 2);
  buffer      += 2;
  writeLen    += 2;

  if (strlen(cmd) == 0) goto end;

  tmpWriteLen  = snprintf((char*)buffer, bufferSz, "%s", cmd);
  buffer      += tmpWriteLen;
  writeLen    += tmpWriteLen;

  if (paramNb > 0) {
    tmpWriteLen  = snprintf((char*)buffer, bufferSz, "=");
    buffer      += tmpWriteLen;
    writeLen    += tmpWriteLen;
  }

  while (paramNb--) {
    switch (params->type) {
    case AT_NUMBER:
      tmpWriteLen  = snprintf((char*)buffer, bufferSz, "%d", params->value.number);
      buffer      += tmpWriteLen;
      writeLen    += tmpWriteLen;
      break;
    
    case AT_STRING:
      tmpWriteLen  = snprintf((char*)buffer, bufferSz, "\"%s\"", params->value.string);
      buffer      += tmpWriteLen;
      writeLen    += tmpWriteLen;
      break;
    
    default: return 0;
    }

    if (paramNb != 0) {
      tmpWriteLen  = snprintf((char*)buffer, bufferSz, ",");
      buffer      += tmpWriteLen;
      writeLen    += tmpWriteLen;
    }
    params++;
  }

end:
  tmpWriteLen  = snprintf((char*)buffer, bufferSz, "\r\n");
  buffer      += tmpWriteLen;
  writeLen    += tmpWriteLen;

  return writeLen;
}

const char *AT_ParseResponse(const char *respStr, AT_Data_t *data)
{
  uint8_t isParsing = 0;
  uint8_t isInStr = 0;
  uint8_t isBinary = 0;
  uint8_t *strOutput = 0;
  size_t outputSZ = 0;

  if (respStr == 0) return 0;

  while (1) {
    if (*respStr == 0) return 0;
    else if (*respStr == '\r') {
      if (isBinary) {
        strOutput = 0;
        break;
      }
      else if (!isInStr) return 0;
    }

    else if (*respStr == ',' && !isInStr) {
      respStr++;
      break;
    }

    else if (*respStr == '\"') {
      if (isInStr) {
        if (isParsing && strOutput != 0)
          strOutput = 0;
        isInStr = 0;
        isParsing = 0;
      }
      else {
        isInStr = 1;
      }
    }

    else if (data != 0) {
      if (!isParsing) {
        isParsing = 1;

        if (isInStr) {
          data->type = AT_STRING;
          data->value.string = (const char*)data->ptr;
          strOutput = data->ptr;
          outputSZ = data->size;
        } else {
          if (*respStr >= '0' && *respStr <= '9') {
            data->type = AT_NUMBER;
            data->value.number = atoi((char*)respStr);
          } else if (data->ptr != 0) {
            data->type = AT_STRING;
            isBinary = 1;
            data->value.string = (const char*)data->ptr;
            strOutput = data->ptr;
            outputSZ = data->size;
          }
        }
      }
      if ((isInStr || isBinary) && outputSZ != 0 && strOutput != 0) {
        *strOutput = (char) *respStr;
        strOutput++;
        outputSZ--;
      }
    }

    respStr++;
  }

  return respStr;
}
