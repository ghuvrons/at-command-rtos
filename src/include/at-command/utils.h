#ifndef AT_COMMAND_UTILS_H
#define AT_COMMAND_UTILS_H

#include "types.h"

const char *AT_ParseData(const char *respStr, AT_Data_t *data);
const char *AT_ParseResponse(const char *respStr, uint8_t respNb, AT_Data_t *respDataPtr);
const char *AT_ParseResponseList(const char *respStr, uint8_t respListSize, uint8_t respNb, AT_Data_t *respDataPtr);
uint16_t AT_WriteCommand(uint8_t *buffer, uint16_t bufferSz, 
                         AT_Command_t, uint8_t paramNb, AT_Data_t *params);


#endif /* AT_COMMAND_UTILS_H */
