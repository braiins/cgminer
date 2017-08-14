#ifndef _ASIC_INNO_
#define _ASIC_INNO_

#define ASIC_CHAIN_NUM		2
#define ASIC_CHIP_NUM		10

#define WEAK_CHIP_THRESHOLD	30
#define BROKEN_CHIP_THRESHOLD 26
#define WEAK_CHIP_SYS_CLK	(600 * 1000)
#define BROKEN_CHIP_SYS_CLK	(400 * 1000)
//#define CHIP_A6 1

#include "asic_inno_cmd.h"

bool check_chip(struct A1_chain *a1, int i);
int chain_detect(struct A1_chain *a1);
bool abort_work(struct A1_chain *a1);

int get_current_ms(void);
bool is_chip_disabled(struct A1_chain *a1, uint8_t chip_id);
void disable_chip(struct A1_chain *a1, uint8_t chip_id);

bool get_nonce(struct A1_chain *a1, uint8_t *nonce, uint8_t *chip_id, uint8_t *job_id);
bool set_work(struct A1_chain *a1, uint8_t chip_id, struct work *work, uint8_t queue_states);
void check_disabled_chips(struct A1_chain *a1);

#endif

