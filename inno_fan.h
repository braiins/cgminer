#ifndef _INNO_FAN_
#define _INNO_FAN_

#include <stdint.h>
#include "logging.h"
#include "miner.h"
#include "util.h"
#ifdef CHIP_A6
#include "A6_inno.h"
#else
#include "A5_inno.h"
#include "A5_inno_cmd.h"
#include "A5_inno_clock.h"
#endif

#define ASIC_INNO_TEMP_CONTRL_THRESHOLD (25.0f)

typedef struct INNO_FAN_CTRL_tag{
    /* 温度原始值 */
	int temp[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
    int index[ASIC_CHAIN_NUM];                  /* chip index in chain */

    /* 以寄存器原始值为格式 */
    int temp_arvarge[ASIC_CHAIN_NUM];           /* 当前温度(均值) */
    int temp_init[ASIC_CHAIN_NUM];              /* 初始温度(均值) */
    int temp_highest[ASIC_CHAIN_NUM];           /* 当前温度(最高) */
    int temp_lowest[ASIC_CHAIN_NUM];            /* 当前温度(最低) */

    /* 保护多链(线程)共享的数据 */
	pthread_mutex_t lock;                       /* 互斥锁 */
    int duty;                                   /* 0 - 100 */

    /* 用于转化寄存器原始值到实际温度 */
    int temp_nums;                              /* temp寄存器与温度对应表 的点数 */
    int temp_v_max;                             /* temp最大值 对应最低温度 */
    int temp_v_min;                             /* temp最小值 对应最高温度 */
    float temp_f_min;                           /* 温度最小值 */
    float temp_f_max;                           /* 温度最大值 */
    float temp_f_step;                          /* 温度步长 */
}INNO_FAN_CTRL_T;

/* 模块初始化 */
void inno_fan_init(INNO_FAN_CTRL_T *fan_ctrl);
/* 设置启动温度 */
void inno_fan_temp_init(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
/* 加入芯片温度 */
void inno_fan_temp_add(INNO_FAN_CTRL_T *fan_ctrl, int chain_id, int temp, bool warn_on);
/* 清空芯片温度,为下轮循环准备 */
void inno_fan_temp_clear(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
/* 根据温度更新转速 */
void inno_fan_speed_update(INNO_FAN_CTRL_T *fan_ctrl, int chain_id, struct cgpu_info *cgpu);

float inno_fan_temp_to_float(INNO_FAN_CTRL_T *fan_ctrl, int temp);

int inno_fan_temp_get_highest(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);

void inno_temp_contrl(INNO_FAN_CTRL_T *fan_ctrl, struct A1_chain *a1, int chain_id);

#if 0
float inno_fan_temp_get(INNO_FAN_CTRL_T *fan_ctrl, int chain_id);
#endif

#if 0
void inno_fan_speed_up(INNO_FAN_CTRL_T *fan_ctrl);
void inno_fan_speed_down(INNO_FAN_CTRL_T *fan_ctrl);
void inno_fan_pwm_set(INNO_FAN_CTRL_T *fan_ctrl, int duty);
#endif

void fancontrol_start(unsigned enabled_chains);

#endif

