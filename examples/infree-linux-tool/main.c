#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <fcntl.h>
#include <termios.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>
#include <serial.h>

static int fd = -1;

uint64_t get_current_time(void)
{
	struct timeval time;

	gettimeofday(&time, 0);
	return (uint64_t)(time.tv_sec * 1000 + time.tv_usec / 1000);
}

void * uart_receive_thread(void * arg)
{
	uint8_t c = 0;

	if(fd != 0)
	{
		while(1)
		{
			if(serial_read_byte(fd, &c) == 1)
			{
				printf("%c", c);
				fflush(stdout);
			}
		}
	}
	return 0;
}

int main(int argc, char *argv[])
{
	char * dev = "/dev/ttyS1";
	char buf[256];
	int len, i = 0;
	pthread_t thread;

	if(argc > 1)
		dev = argv[1];
	fd = serial_open(dev, 9600);
	if(!fd)
		return -1;

	pthread_create(&thread, NULL, uart_receive_thread,NULL);
	while(1)
	{
		len = sprintf(buf, "i = %d\r\n", i++);
		serial_write(fd, buf, len);
		sleep(1);
	}
	serial_close(fd);

	return 0;
}
