#ifndef AT_COMMAND_TYPES_H
#define AT_COMMAND_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define AT_EVT_OK         0x01U
#define AT_EVT_ERROR      0x02U
#define AT_EVT_CMD_RESP   0x04U
#define AT_EVT_BYTES_FLAG 0x08U

typedef enum {
  AT_OK,
  AT_ERROR,
  AT_TIMEOUT,
} AT_Status_t;

typedef enum {
  AT_NUMBER,
  AT_STRING,
  AT_HEX,
} AT_DataType_t;

typedef const char* AT_Command_t;
typedef struct {
  AT_DataType_t type;
  union {
    int     number;
    char    *string;
    uint8_t *bytes;
  } value;
  size_t size;
} AT_Data_t;

typedef struct {
    uint8_t       cmdLen;
    AT_Command_t  cmd;
    uint8_t       respNb;
    AT_Data_t     *resp;
} AT_CmdResp_t;



struct AT_BufferReadTo {
  void *buffer;
  uint16_t length;
};

typedef void (*AT_EH_Callback_t)(void *app, AT_Data_t*);
typedef struct AT_BufferReadTo (*AT_EH_CallbackBufReadTo_t)(void *app, AT_Data_t*);

typedef struct AT_EventHandler_t {
  void          *app;
  AT_CmdResp_t  cmdResp;

  AT_EH_Callback_t callback;
  AT_EH_CallbackBufReadTo_t callbackBufferReadTo;
  struct AT_EventHandler_t *next;
} AT_EventHandler_t;

#define AT_Number(n)        {.type = AT_NUMBER,.value.number = (n),.size=0,}
#define AT_String(str, len) {.type = AT_STRING,.value.string = (str),.size=(len),}
#define AT_Hex(b)           {.type = AT_HEX,.value.bytes = (b),.size=sizeof((b)),}

#define AT_DataSetNumber(data, n) {(data)->type = AT_NUMBER;    \
                                   (data)->value.number = (n);  \
                                   (data)->size=0;}

#define AT_DataSetString(data, str, len) {(data)->type = AT_STRING;    \
                                          (data)->value.string = (str);  \
                                          (data)->size=(len);}

#endif /* AT_COMMAND_TYPES_H */
