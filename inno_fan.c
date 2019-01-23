#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/ioctl.h>
#include <assert.h>
#include <math.h>
#include <time.h>

#include "inno_fan.h"

#define N_PWM_CHIPS 3
#define TEMP_MAX 9999
#define TEMP_MIN 0

#undef TEMP_DEBUG_ENABLED

struct chain_temp {
	int initialized, enabled;
	double min, max, med, avg;
	cgtimer_t time;
};

struct temp_data {
	pthread_mutex_t lock;
	struct chain_temp chain_temps[ASIC_CHAIN_NUM];
	int new_data;
#if 0
	/* temperature log */
	pthread_mutex_t temp_lock;
	FILE *temp_log;
#endif
};

static void set_fanspeed(int id, int duty);
static void set_fanspeed_allfans(int duty);

static struct temp_data temp_data;
static struct fancontrol fancontrol;

static void innofan_update_chain_temp(int chain_id, double min, double max, double med, double avg)
{
	assert(chain_id >= 0);
	assert(chain_id < ASIC_CHAIN_NUM);

	applog_hw(LOG_INFO, "update_chain_temp: chain=%d min=%2.3lf max=%2.3lf med=%2.3lf avg=%2.3lf", chain_id, min, max, med, avg);

	mutex_lock(&temp_data.lock);
	struct chain_temp *temp = &temp_data.chain_temps[chain_id];
	temp->initialized = 1;
	temp->min = min;
	temp->max = max;
	temp->med = med;
	temp->avg = avg;
	temp_data.new_data++;
	cgtimer_time(&temp->time);
	mutex_unlock(&temp_data.lock);
}

/*
 * Calculates min/max/avg over all chains and stores it into "total".
 * Returns 0 if some enabled chains are not initialized, 1 otherwise.
 */
static int calc_min_max_over_chains(struct chain_temp *total)
{
	struct chain_temp *temp;
	int n = 0;

	total->initialized = 1;
	total->min = TEMP_MAX;
	total->max = TEMP_MIN;

	/* take maximu average temperature of all enabled chains */
	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		temp = &temp_data.chain_temps[i];
		if (temp->enabled) {
			if (!temp->initialized) {
				/* we do not have all temperatures */
				return 0;
			}
			if (temp->min < total->min)
				total->min = temp->min;
			if (temp->max > total->max)
				total->max = temp->max;
			if (temp->avg > total->avg)
				total->avg = temp->avg;
			n++;
		}
	}
	return n > 0;
}

static void *innofan_thread(void __maybe_unused *argv)
{
	/* try to adjust fan speed every second */
	int old_duty = FAN_DUTY_MAX;
	for (;;) {
		bool do_fan_update = false;
		int duty = FAN_DUTY_MAX;

		/* everything fan* is protected by lock */
		mutex_lock(&temp_data.lock);
		/* call pid only if new data are available */
		if (temp_data.new_data > 0) {
			struct chain_temp temp;
			int temp_ok = calc_min_max_over_chains(&temp);
			/* feed data to pid */
			duty = fancontrol_calculate(&fancontrol, temp_ok, temp.max);
			do_fan_update = true;
			temp_data.new_data = 0;
		}
		mutex_unlock(&temp_data.lock);

		/* now do the fan update */
		if (do_fan_update) {
			if (duty != old_duty) {
				applog_hw(LOG_INFO, "innofan_thread: duty=%d", duty);
				old_duty = duty;
			}
			set_fanspeed_allfans(duty);
		}
		sleep(1);
	}
}

void innofan_reconfigure_fans(void)
{
	mutex_lock(&temp_data.lock);
	if (opt_fan_ctrl == FAN_MODE_TEMP) {
		/* clamp values to sane range */
		if (opt_fan_temp < MIN_TEMP)
			opt_fan_temp = MIN_TEMP;
		if (opt_fan_temp > HOT_TEMP)
			opt_fan_temp = HOT_TEMP;
		applog_hw(LOG_NOTICE, "AUTOMATIC fan control, target temperature %d degrees", opt_fan_temp);
		fancontrol_setmode_auto(&fancontrol, opt_fan_temp);
	} else if (opt_fan_ctrl == FAN_MODE_SPEED) {
		/* clamp values to sane range */
		if (opt_fan_speed < 0)
			opt_fan_speed = 0;
		if (opt_fan_speed > 100)
			opt_fan_speed = 100;
		applog_hw(LOG_NOTICE, "MANUAL fan control, target speed %d%%", opt_fan_speed);
		fancontrol_setmode_manual(&fancontrol, opt_fan_speed);
	} else {
		applog_hw(LOG_NOTICE, "EMERGENCY fan control, fans to full");
		fancontrol_setmode_emergency(&fancontrol);
	}
	mutex_unlock(&temp_data.lock);
}

void innofan_copy_fancontrol(struct fancontrol *fc)
{
	mutex_lock(&temp_data.lock);
	*fc = fancontrol;
	mutex_unlock(&temp_data.lock);
}

void innofan_start(unsigned enabled_chains)
{
	temp_data.new_data = 1;
	mutex_init(&temp_data.lock);

	set_fanspeed_allfans(FAN_DUTY_MAX);
	fancontrol_init(&fancontrol);
	innofan_reconfigure_fans();

	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		struct chain_temp *temp = &temp_data.chain_temps[i];
		temp->enabled = 0;
		temp->initialized = 0;
		if (enabled_chains & (1u << i))
			temp->enabled = 1;
	}

	pthread_t tid;
	pthread_create(&tid, NULL, innofan_thread, NULL);
}

static int floatcmp(const void *a, const void *b)
{
	const float *fa = a, *fb = b;
	if (*fa < *fb) return -1;
	if (*fa > *fb) return 1;
	return 0;
}

static void temp_calc_minmaxavg(struct A1_chain *chain)
{
	struct A1_chain_temp_stats *stats = &chain->temp_stats;
	int n;
	float sum;
	float temp[MAX_CHAIN_LENGTH];

	/* fill-in safe defaults */
	stats->min = TEMP_MIN;
	stats->max = TEMP_MAX;
	stats->med = TEMP_MAX;
	stats->avg = TEMP_MAX;

	/* gather statistics */
	n = 0;
	sum = 0;
	for (int i = 0; i < chain->num_active_chips; i++) {
		struct A1_chip *chip = &chain->chips[i];
		if (!chip->disabled) {
			temp[n] = chip->temp_f;
			sum += chip->temp_f;
			n++;
		}
	}

	/* all chips disabled - do nothing */
	if (n == 0)
		return;

	/* compute statistics */
	qsort(temp, n, sizeof(float), floatcmp);
	stats->avg = sum / n;
	stats->min = temp[0];
	stats->max = temp[n - 1];
	stats->med = temp[n / 2];
}

void inno_fan_temp_init(struct A1_chain *chain)
{
	temp_calc_minmaxavg(chain);
}

static int write_to_file(const char *path_fmt, int id, const char *data_fmt, ...)
{
	char path[256];
	char data[256];
	va_list ap;
	int fd;
	size_t len;

	snprintf(path, sizeof(path), path_fmt,id);

	va_start(ap, data_fmt);
	vsnprintf(data, sizeof(data), data_fmt, ap);
	va_end(ap);

	a5_debug("writefile: %s <- \"%s\"\n", path, data);

	fd = open(path, O_WRONLY);
	if (fd < 0) {
		applog_hw(LOG_ERR, "open of %s failed", path);
		return 0;
	}
	len = strlen(data);
	if (write(fd, data, len) != len) {
		applog_hw(LOG_ERR, "short write to %s", path);
		return 0;
	}
	close(fd);
	return 1;
}

#define PWMCHIP_SYSFS "/sys/class/pwm/pwmchip%d"
#define PWMCHIP_PERIOD 100000

static int pwm_chip_initialized[N_PWM_CHIPS];

static void set_fanspeed(int id, int duty)
{
	assert(id >= 0 && id < N_PWM_CHIPS);
	if (!pwm_chip_initialized[id]) {
		pwm_chip_initialized[id] = 1;
		write_to_file(PWMCHIP_SYSFS "/export", id, "0");
		write_to_file(PWMCHIP_SYSFS "/pwm0/period", id, "%d", PWMCHIP_PERIOD);
		write_to_file(PWMCHIP_SYSFS "/pwm0/duty_cycle", id, "10000");
		write_to_file(PWMCHIP_SYSFS "/pwm0/enable", id, "1");
	}
	/* do not set the extreme values (full-cycle and no-cycle) just in case */
	/* it shouldn't be buggy, but if it would, setting duty-cycle to 100
	   would effectively stop the fan and cause miner to overheat and
	   explode. */
	write_to_file(PWMCHIP_SYSFS "/pwm0/duty_cycle", id, "%d", (100 - duty)*PWMCHIP_PERIOD/100);
}

static void set_fanspeed_allfans(int duty)
{
	set_fanspeed(0, duty);
	set_fanspeed(1, duty);
	set_fanspeed(2, duty);
}

#ifndef CHIP_A6
extern uint8_t A1Pll1;
extern uint8_t A1Pll2;
extern uint8_t A1Pll3;
extern const struct PLL_Clock PLL_Clk_12Mhz[142];
extern struct A1_chain *chain[ASIC_CHAIN_NUM];
#endif

static void log_chain_temp(struct A1_chain *chain)
{
#if 0
	FILE *f = temp_data.temp_log;
	if (!f)
		return;
	mutex_lock(&temp_data.temp_lock);
	fprintf(f, "%ld %d ", time(0), chain->chain_id);
	for (int i = 0; i < chain->num_active_chips; i++) {
		struct A1_chip *chip = &chain->chips[i];
		if (i > 0)
			fputc(',', f);
		if (chip->disabled)
			fprintf(f, "0");
		else
			fprintf(f, "%3.2f", chip->temp_f);
	}
	fputc('\n', f);
	mutex_unlock(&temp_data.temp_lock);
#endif
}

void inno_fan_speed_update(struct A1_chain *chain, struct cgpu_info *cgpu)
{
	struct A1_chain_temp_stats *temp_stats = &chain->temp_stats;

	temp_calc_minmaxavg(chain);
	log_chain_temp(chain);
	innofan_update_chain_temp(chain->chain_id, temp_stats->min, temp_stats->max, temp_stats->med, temp_stats->avg);

	cgpu->temp = temp_stats->avg;
	cgpu->temp_max = temp_stats->max;
	cgpu->temp_min = temp_stats->min;
	cgpu->fan_duty = 100;

	cgpu->chip_num = chain->num_active_chips;
	cgpu->core_num = chain->num_cores;

	switch(chain->chain_id){
		case 0:cgpu->mhs_av = (double)PLL_Clk_12Mhz[A1Pll1].speedMHz * 2ull * (chain->num_cores);break;
		case 1:cgpu->mhs_av = (double)PLL_Clk_12Mhz[A1Pll2].speedMHz * 2ull * (chain->num_cores);break;
		case 2:cgpu->mhs_av = (double)PLL_Clk_12Mhz[A1Pll3].speedMHz * 2ull * (chain->num_cores);break;
		default:;
	}
}
