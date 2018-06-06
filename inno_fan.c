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
#define FAN_DUTY_MAX 100
/* do not go lower than 60% duty cycle during warmup */
#define FAN_DUTY_MIN_WARMUP 60
#define FAN_DUTY_MIN 10
#define WARMUP_PERIOD_MS (360*1000)

struct chain_temp {
	int initialized, enabled;
	double min, max, med, avg;
	cgtimer_t time;
};

struct fan_control {
	pthread_mutex_t lock;
	struct chain_temp chain_temps[ASIC_CHAIN_NUM];
	PIDControl pid;
	pthread_t fancontrol_tid;
	int new_data;
	int duty;
	FILE *pid_log;
	/* temperature log */
	pthread_mutex_t temp_lock;
	FILE *temp_log;
};

static void set_fanspeed(int id, int duty);

static struct fan_control fan;

static void fancontrol_update_chain_temp(int chain_id, double min, double max, double med, double avg)
{
	struct chain_temp *temp;
	assert(chain_id >= 0);
	assert(chain_id < ASIC_CHAIN_NUM);

	applog_hw(LOG_INFO, "update_chain_temp: chain=%d min=%2.3lf max=%2.3lf med=%2.3lf avg=%2.3lf", chain_id, min, max, med, avg);

	mutex_lock(&fan.lock);
	temp = &fan.chain_temps[chain_id];
	temp->initialized = 1;
	temp->min = min;
	temp->max = max;
	temp->med = med;
	temp->avg = avg;
	fan.new_data++;
	cgtimer_time(&temp->time);
	mutex_unlock(&fan.lock);
}

static void plog(const char *fmt, ...)
{
	va_list ap;
	char buf[128];
	time_t now;
	struct tm tm;

	if (fan.pid_log == 0)
		return;

	time(&now);
	localtime_r(&now, &tm);
	strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
	fprintf(fan.pid_log, "%s ", buf);

	va_start(ap, fmt);
	vfprintf(fan.pid_log, fmt, ap);
	va_end(ap);

	fprintf(fan.pid_log, "\n");
	fflush(fan.pid_log);
}

static int calc_duty(float dt)
{
	struct chain_temp *temp;
	double max, min, avg;
	int n = 0;
	/* default behavior is to run fan at max */
	int duty = FAN_DUTY_MAX;

	/* take maximu average temperature of all enabled chains */
	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		temp = &fan.chain_temps[i];
		if (temp->enabled) {
			/* if not all enabled chains are initialized,
			   do not control fan */
			if (!temp->initialized) {
				duty = FAN_DUTY_MAX;
				goto done;
			}
			if (n == 0 || temp->min < min)
				min = temp->min;
			if (n == 0 || temp->max > max)
				max = temp->max;
			if (n == 0 || temp->avg > avg)
				avg = temp->avg;
			n++;
		}
	}

	/* run PID on average temperature */
	PIDInputSet(&fan.pid, avg);
	PIDCompute(&fan.pid, dt);
	duty = PIDOutputGet(&fan.pid);

	/* turn fan full on if temperature is too hot */
	if (max > HOT_TEMP) {
		plog("very hot!");
		duty = FAN_DUTY_MAX;
	}

done:
	plog("dt=%f min=%f max=%f avg=%f out=%d", dt, min, max, avg, duty);
	return duty;
}

static void *fancontrol_thread(void __maybe_unused *argv)
{
	unsigned long start_time, last_pid_time;
	int warming_up;

	start_time = last_pid_time = get_current_ms();
	warming_up = 1;

	/* try to adjust fan speed every second */
	for (;;sleep(1)) {
		int duty = -1;

		/* everything fan* is protected by lock */
		mutex_lock(&fan.lock);
		/* call pid only if new data are available */
		if (fan.new_data > 0) {
			unsigned long now = get_current_ms();
			/* time delta in seconds since last pid */
			float dt = (now - last_pid_time) / 1000.0;

			/* if we are past warming phase, remove fan speed limits */
			if (warming_up && now - start_time > WARMUP_PERIOD_MS) {
				PIDOutputLimitsSet(&fan.pid, FAN_DUTY_MIN, FAN_DUTY_MAX);
				warming_up = 0;
			}
			/* calculate new fan duty cycle */
			duty = fan.duty = calc_duty(dt);
			fan.new_data = 0;
			last_pid_time = now;
		}
		mutex_unlock(&fan.lock);

		if (duty >= 0) {
			applog_hw(LOG_INFO, "fancontrol_thread: duty=%d", duty);
			set_fanspeed(0, duty);
		}
	}
	return NULL;
}

static void fancontrol_init_pid(void)
{
	float kp = 20;
	float ki = 0.05;
	float kd = 0.1;
	float set_point = TARGET_TEMP;

	PIDInit(&fan.pid, kp, ki, kd, FAN_DUTY_MIN_WARMUP, FAN_DUTY_MAX, AUTOMATIC, REVERSE);
	PIDSetpointSet(&fan.pid, set_point);
	plog("# kp=%f ki=%f kd=%f target=%f", kp, ki, kd, set_point);
}

void fancontrol_start(unsigned enabled_chains)
{
	struct chain_temp *temp;
	fan.new_data = 1;
	mutex_init(&fan.lock);
#ifdef TEMP_DEBUG_ENABLED
	fan.pid_log = fopen("/tmp/PID.log", "w");
	fan.temp_log = fopen("/tmp/temp.log", "w");
#else
	fan.pid_log = 0;
	fan.temp_log = 0;
#endif
	mutex_init(&fan.temp_lock);
	set_fanspeed(0, FAN_DUTY_MAX);
	for (int i = 0; i < ASIC_CHAIN_NUM; i++) {
		temp = &fan.chain_temps[i];
		temp->enabled = 0;
		temp->initialized = 0;
		if (enabled_chains & (1u << i))
			temp->enabled = 1;
	}

	fancontrol_init_pid();
	pthread_create(&fan.fancontrol_tid, NULL, fancontrol_thread, NULL);
}

int floatcmp(const void *a, const void *b)
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
	stats->min = -9999;
	stats->max = 9999;
	stats->med = 9999;
	stats->avg = 9999;

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

#ifndef CHIP_A6
extern uint8_t A1Pll1;
extern uint8_t A1Pll2;
extern uint8_t A1Pll3;
extern const struct PLL_Clock PLL_Clk_12Mhz[142];
extern struct A1_chain *chain[ASIC_CHAIN_NUM];
#endif

static void log_chain_temp(struct A1_chain *chain)
{
	FILE *f = fan.temp_log;
	if (!f)
		return;
	mutex_lock(&fan.temp_lock);
	fprintf(f, "%ld %d ", time(0), chain->chain_id);
	mutex_lock(&fan.lock);
	fprintf(f, "%d ", fan.duty);
	mutex_unlock(&fan.lock);
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
	mutex_unlock(&fan.temp_lock);
}

void inno_fan_speed_update(struct A1_chain *chain, struct cgpu_info *cgpu)
{
	struct A1_chain_temp_stats *temp_stats = &chain->temp_stats;

	temp_calc_minmaxavg(chain);
	log_chain_temp(chain);
	fancontrol_update_chain_temp(chain->chain_id, temp_stats->min, temp_stats->max, temp_stats->med, temp_stats->avg);

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
