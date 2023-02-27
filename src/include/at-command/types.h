#ifndef AT_COMMAND_TYPES_H
#define AT_COMMAND_TYPES_H

#include <stdint.h>
#include <stddef.h>

#define AT_EVT_OK               0x01U
#define AT_EVT_ERROR            0x02U
#define AT_EVT_CMD_RESP         0x04U
#define AT_EVT_BYTES_FLAG_START 0x08U
#define AT_EVT_BYTES_FLAG_END   0x10U

typedef enum {
  AT_OK,
  AT_ERROR,
  AT_TIMEOUT,
} AT_Status_t;

typedef enum {
  AT_UNAVAILABLE,
  AT_NUMBER,
  AT_FLOAT,
  AT_STRING,
  AT_BYTES,
  AT_HEX,
} AT_DataType_t;

typedef const char* AT_Command_t;
typedef struct {
  AT_DataType_t type;
  union {
    int         number;
    float       floatNumber;
    const char  *string;
    uint8_t     *bytes;
  } value;

  uint8_t *ptr;
  size_t  size;
} AT_Data_t;

typedef struct {
    uint8_t       cmdLen;
    AT_Command_t  cmd;
    uint8_t       respListSize;
    uint8_t       respNb;
    AT_Data_t     *resp;
    uint8_t       error;

    // for command readinto
    void      *buffer;
    uint16_t  *readLen;
} AT_CmdResp_t;



struct AT_BufferReadTo {
  void      *buffer;
  uint16_t  readLen;
};

typedef void (*AT_EH_Callback_t)(void *app, AT_Data_t*);
typedef void (*AT_EH_CallbackReadline_t)(void *app, uint8_t*, uint16_t bufferSize);
typedef struct AT_BufferReadTo (*AT_EH_CallbackBufReadTo_t)(void *app, AT_Data_t*);

typedef struct AT_EventHandler_t {
  void          *app;
  AT_CmdResp_t  cmdResp;

  AT_EH_Callback_t          callback;
  AT_EH_CallbackReadline_t  callbackReadline;
  AT_EH_CallbackBufReadTo_t callbackBufferReadTo;

  struct AT_EventHandler_t *next;
} AT_EventHandler_t;

#define AT_Number(n)        {.type = AT_NUMBER, .value.number = (n),      .ptr=0,     .size=0,}
#define AT_Float(n)         {.type = AT_FLOAT,  .value.floatNumber = (n), .ptr=0,     .size=0,}
#define AT_String(str)      {.type = AT_STRING, .value.string = (str),    .ptr=0,     .size=0,}
#define AT_Buffer(buf, len) {.type = AT_STRING, .value.string = 0,        .ptr=(buf), .size=(len),}
#define AT_Bytes(b, len)    {.type = AT_BYTES,  .value.string = (b),      .ptr=0,     .size=(len),}
#define AT_Hex(b)           {.type = AT_HEX,    .value.bytes  = (b),      .ptr=(b),   .size=sizeof((b)),}

#define AT_DataSetNumber(data, n)         {(data)->type = AT_NUMBER; (data)->value.number = (n);}
#define AT_DataSetString(data, str)       {(data)->type = AT_STRING; (data)->value.string = (str);}
#define AT_DataSetBuffer(data, buf, len)  {(data)->type = AT_STRING;                        \
                                           (data)->value.string = (const char*)(data)->ptr; \
                                           (data)->ptr = (buf);                             \
                                           (data)->size = (len);}

#endif /* AT_COMMAND_TYPES_H */
