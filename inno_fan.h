#ifndef _INNO_FAN_
#define _INNO_FAN_

#include <stdint.h>
#include "logging.h"
#include "miner.h"
#include "util.h"
#include "asic_inno.h"

typedef struct INNO_FAN_CTRL_tag{
	int temp[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
    int index[ASIC_CHAIN_NUM];                  /* chip index in chain */
    int duty;                                   /* 0 - 100 */

    float temp_init[ASIC_CHAIN_NUM];            /* 初始温度 */
    float temp_now[ASIC_CHAIN_NUM];             /* 当前温度 */
    float temp_delta[ASIC_CHAIN_NUM];           /* 当前变化率 */
}INNO_FAN_CTRL_T;

/* 模块初始化 */
void inno_fan_init(INNO_FAN_CTRL_T *fan_ctrl);
/* 设置启动温度 */
void inno_fan_temp_init(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
/* 加入芯片温度 */
void inno_fan_temp_add(INNO_FAN_CTRL_T *fan_ctrl, int chain_id, int temp);
/* 清空芯片温度,为下轮循环准备 */
void inno_fan_temp_clear(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
/* 根据温度更新转速 */
void inno_fan_speed_update(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);

#if 0
float inno_fan_temp_get(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
#endif

#if 0
void inno_fan_speed_up(INNO_FAN_CTRL_T *fan_ctrl);
void inno_fan_speed_down(INNO_FAN_CTRL_T *fan_ctrl);
void inno_fan_pwm_set(INNO_FAN_CTRL_T *fan_ctrl, int duty);
#endif

#endif

