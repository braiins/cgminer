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
#include "pid_controller.h"

#define N_PWM_CHIPS 3
#define FAN_DUTY_MAX 0

struct chain_temp {
	int initialized, enabled;
	double min, max, avg;
	cgtimer_t time;
};

struct fan_control {
	int direction;
	double high, low;
};

static struct chain_temp chain_temps[ASIC_CHAIN_NUM];
static pthread_mutex_t fancontrol_lock;
static pthread_t fan_tid;
static struct fan_control fan;
static int fancontrol_in_panic;
static void set_fanspeed(int id, int duty);
static PIDControl temp_pid;
static FILE *pid_log;
static int xxtick;

static void fancontrol_update_chain_temp(int chain_id, double min, double max, double avg)
{
	struct chain_temp *temp;
	assert(chain_id >= 0);
	assert(chain_id < ASIC_CHAIN_NUM);

	printf("update_chain_temp: chain=%d min=%2.3lf max=%2.3lf avg=%2.3lf\n", chain_id, min, max, avg);

	mutex_lock(&fancontrol_lock);
	temp = &chain_temps[chain_id];
	temp->initialized = 1;
	temp->min = min;
	temp->max = max;
	temp->avg = avg;
	cgtimer_time(&temp->time);
	mutex_unlock(&fancontrol_lock);
}

static void fancontrol_panic(int chain_id)
{
	mutex_lock(&fancontrol_lock);
	fancontrol_in_panic = 1;
	mutex_unlock(&fancontrol_lock);
}

#define plog(f,a...) ({ if (pid_log) fprintf(pid_log, f "\n", ##a); fflush(pid_log); })

static int calc_duty(void)
{
	struct chain_temp *temp;
	double max, min, avg;
	int n = 0;
	int duty = FAN_DUTY_MAX;

	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		temp = &chain_temps[i];
		if (temp->enabled) {
			if (!temp->initialized)
				return 0;
			if (n == 0 || temp->min < min)
				min = temp->min;
			if (n == 0 || temp->max > max)
				max = temp->max;
			if (n == 0 || temp->avg > avg)
				avg = temp->avg;
			n++;
		}
	}

	printf("calc_duty: min=%2.3lf max=%2.3lf avg=%2.3lf\n", min, max, avg);
	if (max > HOT_TEMP) {
		plog("# very hot!");
		printf("very hot!\n");
		return FAN_DUTY_MAX;
	}


	PIDInputSet(&temp_pid, avg);
	PIDCompute(&temp_pid);
	duty = 100 - PIDOutputGet(&temp_pid);
	{
		char buf[128];
		time_t now;
		struct tm tm;
		time(&now);
		localtime_r(&now, &tm);
		strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
		plog("%s min=%f max=%f avg=%f out=%d", buf, min, max, avg, duty);
	}
	return duty;
}

static void *fancontrol_thread(void __maybe_unused *argv)
{
	for (;;sleep(5)) {
		int duty;
		mutex_lock(&fancontrol_lock);
		if (fancontrol_panic) {
			printf("fancontrol_thread: !!! PANIC !!! PANIC !!!\n");
			duty = FAN_DUTY_MAX;
		} else {
			duty = calc_duty();
		}
		mutex_unlock(&fancontrol_lock);
		printf("fancontrol_thread: duty=%d\n", duty);
		set_fanspeed(0, duty);
	}
	return NULL;
}

void fancontrol_start(unsigned enabled_chains)
{
	struct chain_temp *temp;
	pid_log = fopen("/tmp/PID.log", "w");
	mutex_init(&fancontrol_lock);
	set_fanspeed(0, 0);
	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		temp = &chain_temps[i];
		temp->enabled = 0;
		temp->initialized = 0;
		if (enabled_chains & (1u << i))
			temp->enabled = 1;
	}

	{
		float kp = 20;
		float ki = 0.05;
		float kd = 0.1;
		float dt = 5;
		float set_point = TARGET_TEMP;

		PIDInit(&temp_pid, kp, ki, kd, dt, 40, 100, AUTOMATIC, REVERSE);
		PIDSetpointSet(&temp_pid, set_point);
		plog("# kp=%f ki=%f kd=%f dt=%f target=%f", kp, ki, kd, dt, set_point);
	}
	pthread_create(&fan_tid, NULL, fancontrol_thread, NULL);
}

static int temp_calc_minmaxavg(struct A1_chain *chain)
{
	struct A1_chain_temp_stats *stats = &chain->temp_stats;
	int min_chip = 0, max_chip = 0;
	int n = 0;
	float min = 0, max = 0, avg = 0;

	for (int i = 0; i < chain->num_active_chips; i++) {
		struct A1_chip *chip = &chain->chips[i];
		if (!chip->disabled) {
			float temp = chip->temp_f;

			if (n == 0) {
				min = max = temp;
				min_chip = max_chip = i;
			} else {
				if (temp < min) {
					min = temp;
					min_chip = i;
				}
				if (temp > max) {
					max = temp;
					max_chip = i;
				}
			}
			avg += temp;
		}
	}
	if (n == 0) {
		stats->min = 9999;
		stats->max = 9999;
		stats->avg = 9999;
		stats->min_chip = 0;
		stats->max_chip = 0;
		stats->avg_chip = 0;
		return 0;
	}

	avg /= n;

	float min_avg_diff = 0;
	int avg_chip = 0;
	int first = 1;

	for (int i = 0; i < chain->num_active_chips; i++) {
		struct A1_chip *chip = &chain->chips[i];
		if (!chip->disabled) {
			float temp = chip->temp_f;
			float diff = fabs(avg - temp);

			if (first || diff < min_avg_diff) {
				min_avg_diff = diff;
				avg_chip = i;
			}
			first = 0;
		}
	}

	stats->min = min;
	stats->min_chip = min_chip;
	stats->max = max;
	stats->max_chip = max_chip;
	stats->avg = avg;
	stats->avg_chip = avg_chip;

	return 1;
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
	write_to_file(PWMCHIP_SYSFS "/pwm0/duty_cycle", id, "%d", duty*PWMCHIP_PERIOD/100);
}

#ifndef CHIP_A6
extern uint8_t A1Pll1;
extern uint8_t A1Pll2;
extern uint8_t A1Pll3;
extern const struct PLL_Clock PLL_Clk_12Mhz[142];
extern struct A1_chain *chain[ASIC_CHAIN_NUM];
#endif

void inno_fan_speed_update(struct A1_chain *chain, struct cgpu_info *cgpu)
{
	struct A1_chain_temp_stats *temp_stats = &chain->temp_stats;

	if (!temp_calc_minmaxavg(chain)) {
		fancontrol_panic(chain->chain_id);
		return;
	}
	fancontrol_update_chain_temp(chain->chain_id, temp_stats->min, temp_stats->max, temp_stats->avg);

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
