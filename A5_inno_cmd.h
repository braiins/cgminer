#ifndef _A5_INNO_CMD_
#define _A5_INNO_CMD_

#include <stdint.h>
#include <pthread.h>
#include <sys/time.h>
#include "elist.h"


#define CMD_BIST_START		0x01
#define CMD_BIST_COLLECT	0x0b
#define CMD_BIST_FIX		0x03
#define CMD_RESET			0x04
#define CMD_RESETBC			0x05
#define CMD_WRITE_JOB		0x0c
#define CMD_READ_RESULT		0x08
#define CMD_WRITE_REG		0x09
#define CMD_READ_REG		0x0a
#define CMD_READ_REG_RESP	0x1a
#define CMD_POWER_ON		0x02
#define CMD_POWER_OFF		0x06
#define CMD_POWER_RESET 	0x0c
#define CMD_READ_SEC_REG    0x0d


#define ADDR_BROADCAST		0x00

#define LEN_BIST_START		6
#define LEN_BIST_COLLECT	4
#define LEN_BIST_FIX		4
#define LEN_RESET			6
#define LEN_WRITE_JOB		94
#define LEN_READ_RESULT		4
#define LEN_WRITE_REG		18
#define LEN_READ_REG		4


#define SPI_REC_DATA_LOOP	10
#define SPI_REC_DATA_DELAY	1

//#define ASIC_REGISTER_NUM	12
#define ASIC_RESULT_LEN		8
#define READ_RESULT_LEN		(ASIC_RESULT_LEN + 2)

#define REG_LENGTH		14
#define JOB_LENGTH		162

#define MAX_CHAIN_LENGTH	64
#define MAX_CMD_LENGTH		(JOB_LENGTH + MAX_CHAIN_LENGTH * 2 * 2)

#define WORK_BUSY 0
#define WORK_FREE 1


struct work_ent {
	struct work *work;
	struct list_head head;
};

struct work_queue {
	int num_elems;
	struct list_head head;
};

struct A1_chip {
	uint8_t reg[12];
	int num_cores;
	int last_queued_id;
	struct work *work[4];
	/* stats */
	int hw_errors;
	int stales;
	int nonces_found;
	int nonce_ranges_done;

	/* systime in ms when chip was disabled */
	int cooldown_begin;
	/* number of consecutive failures to access the chip */
	int fail_count;
	int fail_reset;
	/* mark chip disabled, do not try to re-enable it */
	bool disabled;

	int temp;
};

struct A1_chain {
	int chain_id;
	struct cgpu_info *cgpu;
	struct mcp4x *trimpot;
	int num_chips;
	int num_cores;
	int num_active_chips;
	int chain_skew;
	uint8_t spi_tx[MAX_CMD_LENGTH];
	uint8_t spi_rx[MAX_CMD_LENGTH];
	struct spi_ctx *spi_ctx;
	struct A1_chip *chips;
	pthread_mutex_t lock;

	struct work_queue active_wq;

	/* mark chain disabled, do not try to re-enable it */
	bool disabled;
	uint8_t temp;
	int last_temp_time;

	struct timeval tvScryptLast;
	struct timeval tvScryptCurr;
	struct timeval tvScryptDiff;
	int work_start_delay;
};

struct Test_bench {
	uint32_t uiPll; 
	int uiVol;
	uint32_t uiScore;
	uint32_t uiCoreNum;
};


unsigned short CRC16_2(unsigned char* pchMsg, unsigned short wDataLen);

unsigned short CRC16_2_swap_endian(unsigned char* pchMsg, unsigned short wDataLen);

extern bool inno_cmd_reset(struct A1_chain *pChain, uint8_t chip_id);

extern bool inno_cmd_resetjob(struct A1_chain *pChain, uint8_t chip_id);

extern bool inno_cmd_bist_start(struct A1_chain *pChain, uint8_t chip_id, uint8_t *num);

extern bool inno_cmd_bist_collect(struct A1_chain *pChain, uint8_t chip_id);

extern bool inno_cmd_bist_fix(struct A1_chain *pChain, uint8_t chip_id);

extern bool inno_cmd_write_reg(struct A1_chain *pChain, uint8_t chip_id, uint8_t *reg);

extern bool inno_cmd_read_reg(struct A1_chain *pChain, uint8_t chip_id, uint8_t *reg);

extern bool inno_cmd_read_result(struct A1_chain *pChain, uint8_t chip_id, uint8_t *res);

extern bool inno_cmd_write_job(struct A1_chain *pChain, uint8_t chip_id, uint8_t *job);

extern uint8_t inno_cmd_isBusy(struct A1_chain *pChain, uint8_t chip_id);

extern uint32_t inno_cmd_test_chip(struct A1_chain *pChain);
void inno_cmd_xtest(struct A1_chain *pChain);

extern bool inno_cmd_resetbist(struct A1_chain *pChain, uint8_t chip_id);

void flush_spi(struct A1_chain *pChain);
void hexdump_error(char *prefix, uint8_t *buff, int len);
void hexdump(char *prefix, uint8_t *buff, int len);




#endif
