//Battery Gauge Library V1.0
//© 2016 Texas Instruments Inc.

#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "gauge.h"

#define SET_CFGUPDATE   0x0013
#define CMD_DATA_CLASS  0x3E
#define CMD_BLOCK_DATA  0x40
#define CMD_CHECK_SUM   0x60
#define CMD_FLAGS       0x06
#define CFGUPD          0x0010
#define MAX_ATTEMPTS    5

unsigned int gauge_control(void *pHandle, unsigned int nSubCmd)
{
    unsigned int nResult = 0;
    unsigned char pData[2];
    pData[0] = nSubCmd & 0xFF;
    pData[1] = (nSubCmd >> 8) & 0xFF;
    gauge_write(pHandle, 0x00, pData, 2);
    gauge_read(pHandle, 0x00, pData, 2);
    nResult = (pData[1] << 8) | pData[0];
    return nResult;
}

unsigned int gauge_cmd_read(void *pHandle, unsigned char nCmd)
{
    unsigned char pData[2];
    gauge_read(pHandle, nCmd, pData, 2);
    return (pData[1] << 8) | pData[0];
}

unsigned int gauge_cmd_write(void *pHandle, unsigned char nCmd, unsigned int nData)
{
    unsigned char pData[2];
    pData[0] = nData & 0xFF;
    pData[1] = (nData >> 8) & 0xFF;
    return gauge_write(pHandle, nCmd, pData, 2);
}

bool gauge_cfg_update(void *pHandle)
{
    unsigned int nFlags;
    int nAttempts = 0;
    gauge_control(pHandle, SET_CFGUPDATE);
    do {
        nFlags = gauge_cmd_read(pHandle, CMD_FLAGS);
        if (!(nFlags & CFGUPD)) usleep(500000);
    } while (!(nFlags & CFGUPD) && (nAttempts++ < MAX_ATTEMPTS));
    return (nAttempts < MAX_ATTEMPTS);
}

bool gauge_exit(void *pHandle, unsigned int nCmd)
{
    unsigned int nFlags;
    int nAttempts = 0;
    gauge_control(pHandle, nCmd);
    do {
        nFlags = gauge_cmd_read(pHandle, CMD_FLAGS);
        if (nFlags & CFGUPD) usleep(500000);
    } while ((nFlags & CFGUPD) && (nAttempts++ < MAX_ATTEMPTS));
    return (nAttempts < MAX_ATTEMPTS);
}

static unsigned char check_sum(unsigned char *pData, unsigned char nLength)
{
    unsigned char nSum = 0x00;
    unsigned char n;
    for (n = 0; n < nLength; n++)
        nSum += pData[n];
    nSum = 0xFF - nSum;
    return nSum;
}

int gauge_read_data_class(void *pHandle, unsigned char nDataClass,
                          unsigned char *pData, unsigned char nLength)
{
    unsigned char nRemainder = nLength;
    unsigned int nOffset = 0;
    unsigned char nDataBlock = 0x00;
    unsigned int nData;

    if (nLength < 1) return 0;
    do {
        nLength = nRemainder;
        if (nLength > 32) { nRemainder = nLength - 32; nLength = 32; }
        else nRemainder = 0;

        nData = (nDataBlock << 8) | nDataClass;
        gauge_cmd_write(pHandle, CMD_DATA_CLASS, nData);

        if (gauge_read(pHandle, CMD_BLOCK_DATA, pData, nLength) != nLength) return -1;
        pData += nLength;
        nDataBlock++;
    } while (nRemainder > 0);
    return 0;
}

int gauge_write_data_class(void *pHandle, unsigned char nDataClass,
                           unsigned char *pData, unsigned char nLength)
{
    unsigned char nRemainder = nLength;
    unsigned int nOffset = 0;
    unsigned char pCheckSum[2] = {0x00, 0x00};
    unsigned int nData;
    unsigned char nDataBlock = 0x00;

    if (nLength < 1) return 0;
    do {
        nLength = nRemainder;
        if (nLength > 32) { nRemainder = nLength - 32; nLength = 32; }
        else nRemainder = 0;

        nData = (nDataBlock << 8) | nDataClass;
        gauge_cmd_write(pHandle, CMD_DATA_CLASS, nData);

        if (gauge_write(pHandle, CMD_BLOCK_DATA, pData, nLength) != nLength) return -1;
        pCheckSum[0] = check_sum(pData, nLength);
        gauge_write(pHandle, CMD_CHECK_SUM, pCheckSum, 1);

        usleep(10000);  // 10ms

        gauge_cmd_write(pHandle, CMD_DATA_CLASS, nData);
        gauge_read(pHandle, CMD_CHECK_SUM, pCheckSum + 1, 1);
        if (pCheckSum[0] != pCheckSum[1]) return -2;

        pData += nLength;
        nDataBlock++;
    } while (nRemainder > 0);
    return 0;
}
