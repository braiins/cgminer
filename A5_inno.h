#ifndef _A5_INNO_
#define _A5_INNO_

#define ASIC_CHAIN_NUM                  3
#define ASIC_CHIP_NUM                   63

#define ASIC_CHIP_A_BUCKET              (ASIC_CHAIN_NUM * ASIC_CHIP_NUM)
#define ASIC_INNO_FAN_PWM0_DEVICE_NAME  ("/dev/pwmgen0.0")
//#define ASIC_INNO_FAN_PWM1_DEVICE_NAME  ("/dev/pwmgen0.1")
#define ASIC_INNO_FAN_PWM_STEP          (10)
#define ASIC_INNO_FAN_PWM_DUTY_MAX      (100)
/* 此宏表示PWM载波频率:7KHz,经验值 */
#define ASIC_INNO_FAN_PWM_FREQ_TARGET   (7000)
/* 此宏表示分频比 分频比=50M/PWM载波频率 */
#define ASIC_INNO_FAN_PWM_FREQ          (50000000 / ASIC_INNO_FAN_PWM_FREQ_TARGET)
/* 芯片上电高于100°,告警 */
#define ASIC_INNO_FAN_TEMP_MAX_THRESHOLD (100.0f)
/* chip高于55°后,100% */
#define ASIC_INNO_FAN_TEMP_UP_THRESHOLD (65.0f)
/* chip板低于35°后,60% */
#define ASIC_INNO_FAN_TEMP_DOWN_THRESHOLD (35.0f)

/* 去掉的最高分和最低分比例 */
#define ASIC_INNO_FAN_TEMP_MARGIN_RATE  (5.0f / 72)
/* 数值越小,温度打印得越频繁 */
#define ASIC_INNO_FAN_CTLR_FREQ_DIV     (0)


#define WEAK_CHIP_THRESHOLD	5
#define BROKEN_CHIP_THRESHOLD 5
#define WEAK_CHIP_SYS_CLK	(600 * 1000)
#define BROKEN_CHIP_SYS_CLK	(400 * 1000)

#include "A5_inno_cmd.h"

typedef struct{
   float highest_vol[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
   float lowest_vol[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
   float avarge_vol[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
   float cur_vol[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];    /* chip temp bits */
   int stat_cnt[ASIC_CHAIN_NUM][ASIC_CHIP_NUM];
}inno_reg_ctrl_t;

bool inno_check_voltage(struct A1_chain *a1, int chip_id, inno_reg_ctrl_t *s_reg_ctrl);
void inno_configure_tvsensor(struct A1_chain *a1, int chip_id,bool is_tsensor);

bool check_chip(struct A1_chain *a1, int i);
void prechain_detect(struct A1_chain *a1, int idxpll);
int chain_detect(struct A1_chain *a1);
bool abort_work(struct A1_chain *a1);

int get_current_ms(void);
bool is_chip_disabled(struct A1_chain *a1, uint8_t chip_id);
void disable_chip(struct A1_chain *a1, uint8_t chip_id);

bool get_nonce(struct A1_chain *a1, uint8_t *nonce, uint8_t *chip_id, uint8_t *job_id, uint8_t *micro_job_id);
bool set_work(struct A1_chain *a1, uint8_t chip_id, struct work *work, uint8_t queue_states);
void check_disabled_chips(struct A1_chain *a1, int pllnum);
uint8_t *create_job(uint8_t chip_id, uint8_t job_id, struct work *work);
void test_bench_pll_config(struct A1_chain *a1,uint32_t uiPll);

#define a5_debug(p, q...) ({ if (opt_A5_extra_debug > 0) { fprintf(stderr, "--- " p " ---\n", ##q); } })

#endif

