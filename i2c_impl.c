// BQ34Z100-R2 Linux I2C implementation
// Based on TI SLUA801 gauge communication API
// Target: 2S2P Li-ion, 10Ah, 10mΩ sense resistor, iMX8 I2C bus 2

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include "gauge.h"

// ── Hardware config ───────────────────────────────────────────
#define GAUGE_I2C_BUS       "/dev/i2c-2"
#define GAUGE_DEVICE_ADDR   0xAA        // 8-bit addr = 0x55 << 1

// ── Battery parameters (2S2P, 10Ah, Li-ion) ──────────────────
// CurrScale=2: gauge sense resistor sees current from one 2P path only.
// Actual pack current = gauge reading * CurrScale.
// Design Capacity stored as total_mAh / CurrScale = 10000/2 = 5000.
// Qmax same as Design Capacity.
// VoltScale=1: 8.4V < 65535mV, no scaling needed.
// EnergyScale=1: no scaling needed.
#define CURR_SCALE          2
#define VOLT_SCALE          1
#define ENERGY_SCALE        1
#define DESIGN_CAP_RAW      5000        // mAh = 10000 / CURR_SCALE
#define DESIGN_ENERGY_CWH   3700        // cWh = 5Ah * 7.4V * 100
#define TERMINATE_VOLTAGE   3200        // mV per cell (6400mV pack / 2 cells)
#define TAPER_CURRENT       100         // mA

// ── Standard command codes (TRM Table 2-1) ────────────────────
#define CMD_SOC             0x02
#define CMD_RC              0x04
#define CMD_FCC             0x06
#define CMD_VOLTAGE         0x08
#define CMD_AVG_CURRENT     0x0A
#define CMD_TEMPERATURE     0x0C
#define CMD_FLAGS           0x0E
#define CMD_CURRENT         0x10

// ── Extended command codes (TRM Table 2-8) ────────────────────
// These are confirmed in R2 TRM section 2.2.5-2.2.7
#define CMD_VOLT_SCALE      0x20        // VoltScale()
#define CMD_CURR_SCALE_REG  0x21        // CurrScale()
#define CMD_ENERGY_SCALE    0x22        // EnergyScale()
#define CMD_DESIGN_CAP      0x3C        // DesignCapacity()
#define CMD_SOH             0x2E        // StateOfHealth()
#define CMD_CYCLE_COUNT     0x2C        // CycleCount()
#define CMD_CHG_VOLTAGE     0x30        // ChargeVoltage()
#define CMD_CHG_CURRENT     0x32        // ChargeCurrent()

// ── Control() subcommands (TRM Table 2-2) ─────────────────────
#define SUB_CTRL_STATUS     0x0000
#define SUB_FW_VERSION      0x0002
#define SUB_UNSEAL_1        0x0414
#define SUB_UNSEAL_2        0x3672
#define SUB_IT_ENABLE       0x0021
#define SUB_RESET           0x0041
#define SUB_SEALED          0x0020

// ── Data flash subclasses (TRM Table 7-1) ────────────────────
// Subclass 48 (0x30) = Configuration|Data, 2 blocks (64 bytes)
#define DC_CFG_DATA         0x30
#define DC_CFG_DATA_LEN     64

// Subclass 82 (0x52) = Gas Gauging|State
#define DC_GG_STATE         0x52
#define DC_GG_STATE_LEN     32

// Subclass 80 (0x50) = Gas Gauging|IT Cfg, 3 blocks (96 bytes)
#define DC_IT_CFG           0x50
#define DC_IT_CFG_LEN       96

// ── I2C handle ────────────────────────────────────────────────
typedef struct {
    int           nI2C;
    unsigned char nAddress;
} TI2C;

// ── I2C low-level (SLUA801 Section A.3) ──────────────────────

int gauge_read(void *pHandle, unsigned char nRegister,
               unsigned char *pData, unsigned char nLength)
{
    TI2C *pI2C = (TI2C *)pHandle;
    if (nLength < 1) return 0;
    pData[0] = nRegister;
    write(pI2C->nI2C, pData, 1);
    int n = read(pI2C->nI2C, pData, nLength);
    usleep(100);
    return n;
}

int gauge_write(void *pHandle, unsigned char nRegister,
                unsigned char *pData, unsigned char nLength)
{
    TI2C *pI2C = (TI2C *)pHandle;
    unsigned char pWriteData[nLength + 1];
    if (nLength < 1) return 0;
    pWriteData[0] = nRegister;
    memcpy(pWriteData + 1, pData, nLength);
    int n = write(pI2C->nI2C, pWriteData, nLength + 1);
    usleep(100);
    return n - 1;
}

void gauge_address(void *pHandle, unsigned char nAddress)
{
    TI2C *pI2C = (TI2C *)pHandle;
    if (nAddress != pI2C->nAddress)
        ioctl(pI2C->nI2C, I2C_SLAVE, nAddress >> 1);
    pI2C->nAddress = nAddress;
}

// ── Helpers ───────────────────────────────────────────────────

static void unseal(void *pHandle)
{
    gauge_control(pHandle, SUB_UNSEAL_1); usleep(200000);
    gauge_control(pHandle, SUB_UNSEAL_2); usleep(300000);
}

static const char *get_status(unsigned int flags)
{
    if ((flags >> 9) & 1) return "Full";
    if ((flags >> 8) & 1) return "Charging";
    if (flags & 1)        return "Discharging";
    return "Idle";
}

// ── Configure gauge ───────────────────────────────────────────
// Writes all required parameters per TRM Table 7-1
// Offsets confirmed via BQStudio screenshot + raw block dump

static int configure_gauge(void *pHandle)
{
    int ret;
    unsigned char pData[DC_IT_CFG_LEN];

    printf("\n[CFG] Reading subclass 48 (Configuration|Data)...\n");
    memset(pData, 0, DC_CFG_DATA_LEN);
    ret = gauge_read_data_class(pHandle, DC_CFG_DATA, pData, DC_CFG_DATA_LEN);
    if (ret != 0) { printf("  ERR: read failed (%d)\n", ret); return ret; }

    // ── Block 0 (offsets 0-31) ────────────────────────────────
    // offset 11-12: Design Capacity (I2, MSB first)
    pData[11] = (DESIGN_CAP_RAW >> 8) & 0xFF;
    pData[12] =  DESIGN_CAP_RAW       & 0xFF;

    // offset 13-14: Design Energy (I2, MSB first)
    pData[13] = (DESIGN_ENERGY_CWH >> 8) & 0xFF;
    pData[14] =  DESIGN_ENERGY_CWH       & 0xFF;

    // offset 17-18: Cell Charge Voltage T1-T2 (U2) = 4200 mV
    pData[17] = 0x10; pData[18] = 0x68;

    // offset 19-20: Cell Charge Voltage T2-T3 (U2) = 4200 mV
    pData[19] = 0x10; pData[20] = 0x68;

    // offset 21-22: Cell Charge Voltage T3-T4 (U2) = 4100 mV
    pData[21] = 0x10; pData[22] = 0x04;

    // offset 30: Design Energy Scale (U1) = 1
    pData[30] = VOLT_SCALE;

    // ── Block 1 (offsets 32-63, array index 32-63) ────────────
    // Confirmed via BQStudio screenshot (SLUA screenshot Image 1):
    //   Volt Scale   = subclass 0x30, block 0, offset 30  → pData[30]
    //   Curr Scale   = subclass 0x30, block 0, offset 31  → pData[31]
    //   Energy Scale = subclass 0x30, block 1, offset 0   → pData[32]
    pData[31] = CURR_SCALE;    // offset 31: Curr Scale (confirmed offset!)
    pData[32] = ENERGY_SCALE;  // offset 32 = block1[0]: Energy Scale

    // Device Chemistry (S5) at block1 offset 25 = array index 57
    // 'L','I','O','N','\0'
    pData[57] = 0x4C; pData[58] = 0x49;
    pData[59] = 0x4F; pData[60] = 0x4E;
    pData[61] = 0x00;

    printf("[CFG] Writing subclass 48...\n");
    ret = gauge_write_data_class(pHandle, DC_CFG_DATA, pData, DC_CFG_DATA_LEN);
    printf("  Result: %s\n", ret == 0 ? "OK" : "FAIL");

    // ── Subclass 82: Gas Gauging|State ───────────────────────
    printf("[CFG] Reading subclass 82 (GG State)...\n");
    memset(pData, 0, DC_GG_STATE_LEN);
    gauge_read_data_class(pHandle, DC_GG_STATE, pData, DC_GG_STATE_LEN);

    // offset 0-1: Qmax Cell 0 (I2) = 5000 mAh
    pData[0] = (DESIGN_CAP_RAW >> 8) & 0xFF;
    pData[1] =  DESIGN_CAP_RAW       & 0xFF;

    // offset 4: Update Status = 0x02 (Qmax+Ra learned)
    pData[4] = 0x02;

    // offset 5-6: Cell V at Chg Term (I2) = 4200 mV
    pData[5] = 0x10; pData[6] = 0x68;

    printf("[CFG] Writing subclass 82...\n");
    ret = gauge_write_data_class(pHandle, DC_GG_STATE, pData, DC_GG_STATE_LEN);
    printf("  Result: %s\n", ret == 0 ? "OK" : "FAIL");

    // ── Subclass 80 block 1: Cell Terminate Voltage ──────────
    // offset 53 in IT Cfg = block1[21] = array[53]
    printf("[CFG] Reading subclass 80 (IT Cfg)...\n");
    memset(pData, 0, DC_IT_CFG_LEN);
    gauge_read_data_class(pHandle, DC_IT_CFG, pData, DC_IT_CFG_LEN);

    // offset 53-54: Cell Terminate Voltage (I2) = 3200 mV per cell
    pData[53] = (TERMINATE_VOLTAGE >> 8) & 0xFF;
    pData[54] =  TERMINATE_VOLTAGE       & 0xFF;

    // offset 55-56: Cell Term V Delta (I2) = 200 mV
    pData[55] = 0x00; pData[56] = 0xC8;

    printf("[CFG] Writing subclass 80...\n");
    ret = gauge_write_data_class(pHandle, DC_IT_CFG, pData, DC_IT_CFG_LEN);
    printf("  Result: %s\n", ret == 0 ? "OK" : "FAIL");

    return 0;
}

// ── Main ──────────────────────────────────────────────────────

int main(int argc, char *argv[])
{
    TI2C i2c;
    void *pHandle = (void *)&i2c;

    if ((i2c.nI2C = open(GAUGE_I2C_BUS, O_RDWR)) < 0) {
        printf("Cannot open %s\n", GAUGE_I2C_BUS);
        return 1;
    }
    i2c.nAddress = 0;
    gauge_address(pHandle, GAUGE_DEVICE_ADDR);

    // Identify
    unsigned int fw = gauge_control(pHandle, SUB_FW_VERSION);
    unsigned int cs = gauge_control(pHandle, SUB_CTRL_STATUS);
    printf("FW_VERSION   = 0x%04X\n", fw);
    printf("CTRL_STATUS  = 0x%04X  SS=%d\n", cs, (cs>>13)&1);

    // Unseal if sealed
    if ((cs >> 13) & 1) {
        printf("Unsealing...\n");
        unseal(pHandle);
    }

    // Configure if requested
    if (argc > 1 && strcmp(argv[1], "--configure") == 0) {
        int r = configure_gauge(pHandle);
        if (r == 0) {
            printf("\nResetting gauge...\n");
            gauge_control(pHandle, SUB_RESET);
            sleep(3);
            // Re-unseal after reset
            unseal(pHandle);
        }
    }

    // ── Read and print all live values ────────────────────────
    unsigned int voltage   = gauge_cmd_read(pHandle, CMD_VOLTAGE);
    unsigned int temp_raw  = gauge_cmd_read(pHandle, CMD_TEMPERATURE);
    unsigned int flags     = gauge_cmd_read(pHandle, CMD_FLAGS);
    unsigned int soc       = gauge_cmd_read(pHandle, CMD_SOC) & 0xFF;
    unsigned int rc        = gauge_cmd_read(pHandle, CMD_RC);
    unsigned int fcc       = gauge_cmd_read(pHandle, CMD_FCC);
    unsigned int dcap      = gauge_cmd_read(pHandle, CMD_DESIGN_CAP);
    unsigned int soh       = gauge_cmd_read(pHandle, CMD_SOH);
    unsigned int cyc       = gauge_cmd_read(pHandle, CMD_CYCLE_COUNT);
    unsigned int chgv      = gauge_cmd_read(pHandle, CMD_CHG_VOLTAGE);
    unsigned int chgi      = gauge_cmd_read(pHandle, CMD_CHG_CURRENT);
    // Single-byte extended commands (confirmed R2 TRM Table 2-8)
    unsigned int vs        = gauge_cmd_read(pHandle, CMD_VOLT_SCALE)   & 0xFF;
    unsigned int cs_val    = gauge_cmd_read(pHandle, CMD_CURR_SCALE_REG) & 0xFF;
    unsigned int es        = gauge_cmd_read(pHandle, CMD_ENERGY_SCALE) & 0xFF;

    int current     = (int)(gauge_cmd_read(pHandle, CMD_CURRENT));
    if (current > 32767) current -= 65536;
    int avg_current = (int)(gauge_cmd_read(pHandle, CMD_AVG_CURRENT));
    if (avg_current > 32767) avg_current -= 65536;
    double temp_c = (temp_raw - 2731) / 10.0;

    int chg = (flags >> 8) & 1;
    int fc  = (flags >> 9) & 1;
    int dsg = flags & 1;
    int cf  = (flags >> 4) & 1;

    printf("\n========================================\n");
    printf("  BQ34Z100-R2  STATUS\n");
    printf("========================================\n");
    printf("  Status          : %s\n",   get_status(flags));
    printf("  Voltage         : %u mV\n", voltage);
    printf("  Temperature     : %.1f C\n", temp_c);
    printf("  SOC             : %u %%\n", soc);
    printf("  Current (inst)  : %d mA\n", current);
    printf("  Current (avg)   : %d mA\n", avg_current);
    printf("  Remaining Cap   : %u mAh  (actual=%u mAh)\n", rc, rc * cs_val);
    printf("  Full Charge Cap : %u mAh  (actual=%u mAh)\n", fcc, fcc * cs_val);
    printf("  Design Cap      : %u mAh\n", dcap);
    printf("  StateOfHealth   : %u %%\n", soh);
    printf("  CycleCount      : %u\n",    cyc);
    printf("  ChargeVoltage   : %u mV\n", chgv);
    printf("  ChargeCurrent   : %u mA\n", chgi);
    printf("  VoltScale=%u  CurrScale=%u  EnergyScale=%u\n", vs, cs_val, es);
    printf("  Flags=0x%04X  CHG=%d FC=%d DSG=%d CF=%d\n",
           flags, chg, fc, dsg, cf);

    if (fcc < 1000)
        printf("\n  NOTE: FCC=%u mAh — run learning cycle for accuracy\n", fcc);

    printf("========================================\n");

    close(i2c.nI2C);
    return 0;
}