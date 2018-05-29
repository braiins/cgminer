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

#define DANGEROUS_TEMP	110
#define HOT_TEMP	95
#define TARGET_TEMP	75

void inno_fan_temp_init(struct A1_chain *chain);
void inno_fan_speed_update(struct A1_chain *chain, struct cgpu_info *cgpu);

static inline float inno_fan_temp_get_highest(struct A1_chain *chain)
{
	return chain->temp_stats.max;
}

void fancontrol_start(unsigned enabled_chains);

static inline float inno_temp_to_celsius(int reg)
{
	return (588.0f - reg) * 2 / 3 + 0.5f;
}

#endif

