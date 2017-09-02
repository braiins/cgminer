#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/ioctl.h>

#include "inno_fan.h"

#define MAGIC_NUM  100 

#define IOCTL_SET_FREQ_0 _IOR(MAGIC_NUM, 0, char *)
#define IOCTL_SET_DUTY_0 _IOR(MAGIC_NUM, 1, char *)
#define IOCTL_SET_FREQ_1 _IOR(MAGIC_NUM, 2, char *)
#define IOCTL_SET_DUTY_1 _IOR(MAGIC_NUM, 3, char *)

static int inno_fan_temp_compare(const void *a, const void *b);
static void inno_fan_speed_max(INNO_FAN_CTRL_T *fan_ctrl);
static void inno_fan_pwm_set(INNO_FAN_CTRL_T *fan_ctrl, int duty);

void inno_fan_init(INNO_FAN_CTRL_T *fan_ctrl)
{
    int chain_id = 0;

#if 0 /* 测试风扇 */
    int j = 0;

	for(j = 100; j > 0; j -= 10)
	{
        applog(LOG_ERR, "set test duty:%d", j);
        inno_fan_pwm_set(fan_ctrl, j);
        sleep(5);
	}

	for(j = 0; j < 100; j += 10)
    {
        applog(LOG_ERR, "down test duty.");
        inno_fan_speed_down(fan_ctrl);
        sleep(1);
    }

	for(j = 0; j < 100; j += 10)
    {
        applog(LOG_ERR, "up test duty.");
        inno_fan_speed_up(fan_ctrl);
        sleep(1);
    }
#endif
    inno_fan_pwm_set(fan_ctrl, 10); /* 90% */
    sleep(1);
    inno_fan_pwm_set(fan_ctrl, 20); /* 80% */
    sleep(1);
    inno_fan_pwm_set(fan_ctrl, 30); /* 70% */
    sleep(1);
    inno_fan_pwm_set(fan_ctrl, 20); /* 80% */
    sleep(1);
    inno_fan_pwm_set(fan_ctrl, 10); /* 90% */
    sleep(1);

    for(chain_id = 0; chain_id < ASIC_CHAIN_NUM; chain_id++)
    {
        inno_fan_temp_clear(fan_ctrl, chain_id);
    }

	applog(LOG_ERR, "chip nums:%d.", ASIC_CHIP_A_BUCKET);
	applog(LOG_ERR, "pwm  name:%s.", ASIC_INNO_FAN_PWM0_DEVICE_NAME);
	applog(LOG_ERR, "pwm  step:%d.", ASIC_INNO_FAN_PWM_STEP);
	applog(LOG_ERR, "duty max: %d.", ASIC_INNO_FAN_PWM_DUTY_MAX);
	applog(LOG_ERR, "targ freq:%d.", ASIC_INNO_FAN_PWM_FREQ_TARGET);
	applog(LOG_ERR, "freq rate:%d.", ASIC_INNO_FAN_PWM_FREQ);
}

void inno_fan_temp_add(INNO_FAN_CTRL_T *fan_ctrl, int chain_id, int temp, bool warn_on)
{
    int index = 0;

    index = fan_ctrl->index[chain_id];

    applog(LOG_DEBUG, "inno_fan_temp_add:chain_%d,chip_%d,temp:%08x(%d)", chain_id, index, temp, temp);

    fan_ctrl->temp[chain_id][index] = temp;
    index++;
    fan_ctrl->index[chain_id] = index; 

    /* 避免工作中输出 温度告警信息,影响算力 */
    if(!warn_on)
    {
        return;
    }

    /* 有芯片温度过高,输出告警打印 */
    /* applog(LOG_ERR, "inno_fan_temp_add: temp warn_on(init):%d\n", warn_on); */
    if(temp < ASIC_INNO_FAN_TEMP_THRESHOLD)
    {
        applog(LOG_DEBUG, "inno_fan_temp_add:chain_%d,chip_%d,%08x(%d) is too high!\n", chain_id, index, temp, temp);
    }
}

float inno_fan_temp_get(INNO_FAN_CTRL_T *fan_ctrl, int chain_id)
{
    int   i = 0;
    int   temp_nums = 0;
    int   head_index = 0;
    int   tail_index = 0;
    float arvarge_temp = 0.0f;

    temp_nums = fan_ctrl->index[chain_id];

    /* step1: sort: min to max */
    applog(LOG_DEBUG, "not sort:");
    for(i = 0; i < temp_nums; i++)
    {
        applog(LOG_DEBUG, "chip_%d:%08x(%d)", i, fan_ctrl->temp[chain_id][i], fan_ctrl->temp[chain_id][i]);
    }
    applog(LOG_DEBUG, "sorted:");
    qsort(fan_ctrl->temp[chain_id], temp_nums, sizeof(fan_ctrl->temp[chain_id][0]), inno_fan_temp_compare);
    for(i = 0; i < temp_nums; i++)
    {
        applog(LOG_DEBUG, "chip_%d:%08x(%d)", i, fan_ctrl->temp[chain_id][i], fan_ctrl->temp[chain_id][i]);
    }
    applog(LOG_DEBUG, "sort end.");

#if 0
    /* step2: delete temp (0,1/3) & (2/3,1) */
    head_index = fan_ctrl->index[chain_id] / 3;
    tail_index = fan_ctrl->index[chain_id] - head_index;
#else
    /* step2: delete temp (0, ASIC_INNO_FAN_TEMP_MARGIN_NUM) & (max - ASIC_INNO_FAN_TEMP_MARGIN_NUM, max) */
    head_index = ASIC_INNO_FAN_TEMP_MARGIN_NUM;
    tail_index = fan_ctrl->index[chain_id] - head_index;
#endif

    /* step3: arvarge */
    for(i = head_index; i < tail_index; i++)
    {
        arvarge_temp += fan_ctrl->temp[chain_id][i];
    }
    arvarge_temp /= (tail_index - head_index);

	applog(LOG_DEBUG, "inno_fan_temp_get, chain_id:%d, temp nums:%d, valid index[%d,%d], reseult:%7.4f.\n",
            chain_id, temp_nums, head_index, tail_index , arvarge_temp); 

    inno_fan_temp_clear(fan_ctrl, chain_id);
    return arvarge_temp;
}

void inno_fan_temp_clear(INNO_FAN_CTRL_T *fan_ctrl, int chain_id)
{
    int i = 0;

    fan_ctrl->index[chain_id] = 0;
    for(i = 0; i < ASIC_CHIP_NUM; i++)
    {
        fan_ctrl->temp[chain_id][i] = 0;
    }
}

void inno_fan_temp_init(INNO_FAN_CTRL_T *fan_ctrl, int chain_id)
{
    float temp = 0.0f;
    temp = inno_fan_temp_get(fan_ctrl, chain_id);

    fan_ctrl->temp_init[chain_id] = temp;
    fan_ctrl->temp_now[chain_id] = temp;
    fan_ctrl->temp_delta[chain_id] = 0.0f;
}

void inno_fan_pwm_set(INNO_FAN_CTRL_T *fan_ctrl, int duty)
{
    int fd = 0;
    int duty_driver = 0;

    duty_driver = ASIC_INNO_FAN_PWM_FREQ_TARGET / 100 * duty;

    /* 开启风扇结点 */
    fd = open(ASIC_INNO_FAN_PWM0_DEVICE_NAME, O_RDWR);
    if(fd < 0)
    {
        applog(LOG_ERR, "open %s fail", ASIC_INNO_FAN_PWM0_DEVICE_NAME);
        while(1);
    }

    if(ioctl(fd, IOCTL_SET_FREQ_0, ASIC_INNO_FAN_PWM_FREQ) < 0)
    {
        applog(LOG_ERR, "set fan0 frequency fail");
        while(1);
    }

    if(ioctl(fd, IOCTL_SET_DUTY_0, duty_driver) < 0)
    {
        applog(LOG_ERR, "set duty fail \n");
        while(1);
    }

    close(fd);

    fan_ctrl->duty = duty;
}

void inno_fan_speed_up(INNO_FAN_CTRL_T *fan_ctrl)
{
    int duty = 0;
    
    /* 已经到达最大值,不调 */
    if(0 == fan_ctrl->duty)
    {
        return;
    }

    duty = fan_ctrl->duty;
    duty -= ASIC_INNO_FAN_PWM_STEP;
    if(duty < 0)
    {
        duty = 0;
    } 
    applog(LOG_DEBUG, "speed+(%02d%% to %02d%%)" , 100 - fan_ctrl->duty, 100 - duty);
    fan_ctrl->duty = duty;

    inno_fan_pwm_set(fan_ctrl, duty);
}

void inno_fan_speed_down(INNO_FAN_CTRL_T *fan_ctrl)
{
    int duty = 0;

    /* 已经到达最小值,不调 */
    if(ASIC_INNO_FAN_PWM_DUTY_MAX == fan_ctrl->duty)
    {
        return;
    }

    duty = fan_ctrl->duty;
    duty += ASIC_INNO_FAN_PWM_STEP;
    if(duty > ASIC_INNO_FAN_PWM_DUTY_MAX)
    {
        duty = ASIC_INNO_FAN_PWM_DUTY_MAX;
    }
    applog(LOG_DEBUG, "speed-(%02d%% to %02d%%)" , 100 - fan_ctrl->duty, 100 - duty);
    fan_ctrl->duty = duty;

    inno_fan_pwm_set(fan_ctrl, duty);
}

void inno_fan_speed_update(INNO_FAN_CTRL_T *fan_ctrl, int chain_id)
{
    static int times = 0;       /* 降低风扇控制的频率 */
    float temp_last;            /* 上次温度 */
    float temp_init;            /* 初始温度 */
    float temp_now;             /* 当前温度 */
    float temp_delta;           /* 当前变化率 */


    /* 获取&清空 temp buf */
    temp_now = inno_fan_temp_get(fan_ctrl, chain_id);
    temp_last = fan_ctrl->temp_now[chain_id];
    temp_delta = temp_now - temp_last; 

    /* 降低风扇控制的频率 */
    if(times++ <  ASIC_INNO_FAN_CTLR_FREQ_DIV)
    {
        return;
    }
    applog(LOG_DEBUG, "inno_fan_speed_updat times:%d" , times);
    times = 0;
    
    /* fan_ctrl->temp_init[chain_id] */
    fan_ctrl->temp_now[chain_id] = temp_now;
    fan_ctrl->temp_delta[chain_id] = temp_delta;

    //applog(LOG_DEBUG, "chain_%d, init:%7.4f,now:%7.4f,delta:%7.4f",
    applog(LOG_DEBUG, "chain_%d, init:%7.4f,now:%7.4f,delta:%7.4f",
            chain_id, fan_ctrl->temp_init[chain_id], fan_ctrl->temp_now[chain_id], fan_ctrl->temp_delta[chain_id]);
    
#if 0
    /* 控制策略1(否定,temp_init温度很低,风扇最大依然不够)
     * temp_init为初始值
     * temp_now - temp_init;
     *
     * temp_now > temp_init 表示 temp 较低, speed down
     * else                 表示 temp 较高, speed up
     */
    if(fan_ctrl->temp_now[chain_id] > fan_ctrl->temp_init[chain_id])
    {
        //inno_fan_speed_down();
        applog(LOG_ERR, "- to %d" , fan_ctrl->duty);
    }
    else
    {
        //inno_fan_speed_up();
        applog(LOG_ERR, "+ to %d" , fan_ctrl->duty);
    }
#endif

#if 1
    /* 控制策略2 OK
     * temp_delta为与上次温度的差值
     *
     * temp_delta > 0 表示 temp 较低, speed down
     * else           表示 temp 较高, speed up
     */
    if(fan_ctrl->temp_delta[chain_id] > 0)
    {
        inno_fan_speed_down(fan_ctrl);
    }
    else
    {
        inno_fan_speed_up(fan_ctrl);
    }

    /* 过温保护 */
    if(fan_ctrl->temp_now[chain_id] < ASIC_INNO_FAN_TEMP_THRESHOLD)
    {
        inno_fan_speed_max(fan_ctrl);
        applog(LOG_DEBUG, "temp is too high, speed max 100%%");
    }
#endif
}

static void inno_fan_speed_max(INNO_FAN_CTRL_T *fan_ctrl)
{
    inno_fan_pwm_set(fan_ctrl, 0);
}

static int inno_fan_temp_compare(const void *a, const void *b)
{
    return *(int *)a - *(int *)b;
}

