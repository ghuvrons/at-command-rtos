#ifndef AT_COMMAND_UTILS_H
#define AT_COMMAND_UTILS_H

#include "types.h"

const char *AT_ParseResponse(const char *respStr, AT_Data_t *data);
uint16_t AT_WriteCommand(uint8_t *buffer, uint16_t bufferSz, 
                         AT_Command_t, uint8_t paramNb, AT_Data_t *params);


#endif /* AT_COMMAND_UTILS_H */
