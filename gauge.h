//Battery Gauge Library V1.0
//© 2016 Texas Instruments Inc.

#ifndef __GAUGE_H
#define __GAUGE_H

#include <stdbool.h>

#define SOFT_RESET 0x0042

unsigned int gauge_control(void *pHandle, unsigned int nSubCmd);
unsigned int gauge_cmd_read(void *pHandle, unsigned char nCmd);
unsigned int gauge_cmd_write(void *pHandle, unsigned char nCmd, unsigned int nData);
bool gauge_cfg_update(void *pHandle);
bool gauge_exit(void *pHandle, unsigned int nCmd);
int gauge_read_data_class(void *pHandle, unsigned char nDataClass, unsigned char *pData, unsigned char nLength);
int gauge_write_data_class(void *pHandle, unsigned char nDataClass, unsigned char *pData, unsigned char nLength);
char *gauge_execute_fs(void *pHandle, char *pFS);
int gauge_read(void *pHandle, unsigned char nRegister, unsigned char *pData, unsigned char nLength);
int gauge_write(void *pHandle, unsigned char nRegister, unsigned char *pData, unsigned char nLength);
void gauge_address(void *pHandle, unsigned char nAddress);

#endif
