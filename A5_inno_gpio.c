#include <stdlib.h>
#include <assert.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#include "A5_inno.h"
#include "A5_inno_gpio.h"

#define MAGIC_NUM  100 

#define IOCTL_SET_VAL_0 _IOR(MAGIC_NUM, 0, char *)

int SPI_PIN_POWER_EN[] = {
872,
873,
874,
875
};

int SPI_PIN_START_EN[] = {
854,
856,
858,
860,
};

int SPI_PIN_RESET[] = {
855,
857,
859,
861,
};

void set_vid_value(int level)
{
	int fd; 
    
    printf("%s:%d.\n", __func__, level);

    fd = open(SYSFS_VID_DEV, O_RDWR);
    if(fd < 0)
    {
        fprintf(stderr, "open %s fail.\n", SYSFS_VID_DEV);
        return;
    }

    if(ioctl(fd, IOCTL_SET_VAL_0, 0x80 | level) < 0)
    {
        fprintf(stderr, "set vid value fail.\n");
        return;
    }
    close(fd);	
}

void asic_spi_init(void)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	fd = open(SYSFS_SPI_EXPORT, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%s", "fclk1");
	write(fd, fvalue, strlen(fvalue));
	close(fd);
}

uint32_t set_spi_speed(uint32_t speed)
{
	int fd; 							  
	char fvalue[64];				  
	char fpath[64]; 				  
	uint32_t rdspeed;					  
									  
	fd = open(SYSFS_SPI_VAL_STR, O_WRONLY);
	if(fd == -1)						   
	{									   
			return -1;	
	}					
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", speed*16);	  
	write(fd, fvalue, strlen(fvalue));
	close(fd);						  
										   
	fd = open(SYSFS_SPI_VAL_STR, O_RDONLY);
	if(fd == -1)						   
	{									   
			return -1;					   
	}								  
	memset(fvalue, 0, sizeof(fvalue));
	read(fd, fvalue, 12);			  
	rdspeed = atoi(fvalue); 		  
									  
	return rdspeed;

}

uint32_t get_spi_speed(void)
{
	int fd; 																	  
	char fvalue[64];															  
	uint32_t speed;																	  
																				  
	fd = open(SYSFS_SPI_VAL_STR, O_RDONLY); 									  
	if(fd == -1)					  
	{								  
			return -1;				  
	}									  
	memset(fvalue, 0, sizeof(fvalue));	  
	read(fd, fvalue, 12);				  
	speed = atoi(fvalue);				  
										  
	return speed;
}



void asic_gpio_init(int gpio, int direction)
{
	int fd;
	char fvalue[64];
	char fpath[64];

	fd = open(SYSFS_GPIO_EXPORT, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	memset(fvalue, 0, sizeof(fvalue));
	sprintf(fvalue, "%d", gpio);
	write(fd, fvalue, strlen(fvalue));
	close(fd);

	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);	
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	if(direction == 0)
	{
		write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
	}
	else
	{
		write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
	}	
	close(fd);
}

void asic_gpio_write(int gpio, int value)
{
	int fd;
	char fvalue[64];
	char fpath[64];

#if 0
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);	
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}
	write(fd, SYSFS_GPIO_DIR_OUT, sizeof(SYSFS_GPIO_DIR_OUT));
	close(fd);
#endif	
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		return;
	}

	if(value == 0)
	{
		write(fd, SYSFS_GPIO_VAL_LOW, sizeof(SYSFS_GPIO_VAL_LOW));
	}
	else
	{
		write(fd, SYSFS_GPIO_VAL_HIGH, sizeof(SYSFS_GPIO_VAL_HIGH));
	}	
	close(fd);	
}

int asic_gpio_read(int gpio)
{
	int fd;
	char fvalue[64];
	char fpath[64];

#if 0
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_DIR_STR, gpio);
	fd = open(fpath, O_WRONLY);
	if(fd == -1)
	{
		//printf("%s\n", strerror(errno));
		return -1;
	}
	write(fd, SYSFS_GPIO_DIR_IN, sizeof(SYSFS_GPIO_DIR_IN));
	close(fd);
#endif	
	memset(fpath, 0, sizeof(fpath));
	sprintf(fpath, SYSFS_GPIO_VAL_STR, gpio);
	fd = open(fpath, O_RDONLY);
	if(fd == -1)
	{
		return -1;
	}
	memset(fvalue, 0, sizeof(fvalue));
	read(fd, fvalue, 1);
	if(fvalue[0] == '0')
	{
		return 0;
	}
	else
	{
		return 1;
	}	
}  

