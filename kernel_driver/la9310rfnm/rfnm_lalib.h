/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0)
 * Copyright 2017, 2021-2022 NXP
 */

#ifndef __LA9310_RFNM_LALIB_H__
#define __LA9310_RFNM_LALIB_H__


#define TCML_START 0x1c000000
#define HIF_OFFSET 0x1C000
#define HIF_START  ( TCML_START + HIF_OFFSET )
#define HIF_SIZE   0x4000

#define CCSR_START 0x18000000
#define CCSR_SIZE  0x10000000


#pragma pack(push)




#define RF_SWCMD_DATA_SIZE    12







#define RF_SWCMD_TIMEOUT_SECS    30
#define RF_SWCMD_TIMEOUT_RETRIES 5
#define RFDEV_NAME_LEN 32

#define SET_PARAM(x) data->x = x;
#define GET_PARAM(x) *x = data->x;

#define RF_API_PRE(type, cmdid) \
    type * data; \
    struct rf_sw_cmd_desc sw_cmd = {0}; \
    int ret; \
    sw_cmd.cmd = cmdid; \
    data = (type *)&sw_cmd.data[0]; \

#define RF_API_SEND(type) \
    ret = rf_send_swcmd(rfdev, &sw_cmd, sizeof(type));

#define RF_API_END \
    rf_free_cmd(rfdev, &sw_cmd); \
    return ret; \

struct rf_host_stats {
    int sw_cmds_tx;
    int sw_cmds_failed;
    int sw_cmds_timed_out;
    int sw_cmds_desc_busy;
};

typedef enum modem_endianness {
    MOD_BE,
    MOD_LE
} mod_endian_t;

typedef enum modem_type {
    MOD_GEUL,
    MOD_LA9310,
} mod_type_t;

struct rfdevice {
    char name[RFDEV_NAME_LEN];
    void *hif_p;
    void *rfic_p;
    void *ccsr_p;
    void *scr_p;
    void *scrmodshare_p;
    void *cal_p;
    void *iqdata_p;
    mod_endian_t endian;
    mod_type_t type;
    int act_modem_cnt;
    void *mil_ptr;
	struct rf_host_stats host_stats;
};


#pragma pack(pop)

#define LA9310_RF_SW_CMD_MSG_UNIT_BIT    ( 1 )

#endif
