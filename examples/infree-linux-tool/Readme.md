# 后屏驱动板通信协议

***

## 后屏驱动板采用USB口与上位机进行通讯，上位机PC或android板卡为主机，下位机驱动版为从机

通信协议由帧头，后续帧长度，命令，数据，CRC校验组成。

| 帧头           | 长度               | 命令     | 数据             | CRC          |
| -------------- | ------------------ | -------- | ---------------- | ------------ |
| 0x58，一个字节 | 四个字节，小端模式 | 一个字节 | 任意大于一个字节 | 一个字节CRC8 |

当下位机成功接收一个合法帧后，则会向上位机应答一个ACK字节`0x58`

## CRC8校验算法
```c
/*
 * CRC table for the CRC-8. ( x^8 + x^5 + x^4 + x^0)
 */
static const uint8_t crc8_table[256] = {
	0x00, 0x5e, 0xbc, 0xe2, 0x61, 0x3f, 0xdd, 0x83,
	0xc2, 0x9c, 0x7e, 0x20, 0xa3, 0xfd, 0x1f, 0x41,
	0x9d, 0xc3, 0x21, 0x7f, 0xfc, 0xa2, 0x40, 0x1e,
	0x5f, 0x01, 0xe3, 0xbd, 0x3e, 0x60, 0x82, 0xdc,
	0x23, 0x7d, 0x9f, 0xc1, 0x42, 0x1c, 0xfe, 0xa0,
	0xe1, 0xbf, 0x5d, 0x03, 0x80, 0xde, 0x3c, 0x62,
	0xbe, 0xe0, 0x02, 0x5c, 0xdf, 0x81, 0x63, 0x3d,
	0x7c, 0x22, 0xc0, 0x9e, 0x1d, 0x43, 0xa1, 0xff,
	0x46, 0x18, 0xfa, 0xa4, 0x27, 0x79, 0x9b, 0xc5,
	0x84, 0xda, 0x38, 0x66, 0xe5, 0xbb, 0x59, 0x07,
	0xdb, 0x85, 0x67, 0x39, 0xba, 0xe4, 0x06, 0x58,
	0x19, 0x47, 0xa5, 0xfb, 0x78, 0x26, 0xc4, 0x9a,
	0x65, 0x3b, 0xd9, 0x87, 0x04, 0x5a, 0xb8, 0xe6,
	0xa7, 0xf9, 0x1b, 0x45, 0xc6, 0x98, 0x7a, 0x24,
	0xf8, 0xa6, 0x44, 0x1a, 0x99, 0xc7, 0x25, 0x7b,
	0x3a, 0x64, 0x86, 0xd8, 0x5b, 0x05, 0xe7, 0xb9,
	0x8c, 0xd2, 0x30, 0x6e, 0xed, 0xb3, 0x51, 0x0f,
	0x4e, 0x10, 0xf2, 0xac, 0x2f, 0x71, 0x93, 0xcd,
	0x11, 0x4f, 0xad, 0xf3, 0x70, 0x2e, 0xcc, 0x92,
	0xd3, 0x8d, 0x6f, 0x31, 0xb2, 0xec, 0x0e, 0x50,
	0xaf, 0xf1, 0x13, 0x4d, 0xce, 0x90, 0x72, 0x2c,
	0x6d, 0x33, 0xd1, 0x8f, 0x0c, 0x52, 0xb0, 0xee,
	0x32, 0x6c, 0x8e, 0xd0, 0x53, 0x0d, 0xef, 0xb1,
	0xf0, 0xae, 0x4c, 0x12, 0x91, 0xcf, 0x2d, 0x73,
	0xca, 0x94, 0x76, 0x28, 0xab, 0xf5, 0x17, 0x49,
	0x08, 0x56, 0xb4, 0xea, 0x69, 0x37, 0xd5, 0x8b,
	0x57, 0x09, 0xeb, 0xb5, 0x36, 0x68, 0x8a, 0xd4,
	0x95, 0xcb, 0x29, 0x77, 0xf4, 0xaa, 0x48, 0x16,
	0xe9, 0xb7, 0x55, 0x0b, 0x88, 0xd6, 0x34, 0x6a,
	0x2b, 0x75, 0x97, 0xc9, 0x4a, 0x14, 0xf6, 0xa8,
	0x74, 0x2a, 0xc8, 0x96, 0x15, 0x4b, 0xa9, 0xf7,
	0xb6, 0xe8, 0x0a, 0x54, 0xd7, 0x89, 0x6b, 0x35,
};

static uint8_t crc8_byte(uint8_t crc, const uint8_t data)
{
	return crc8_table[crc ^ data];
}

static uint8_t crc8(uint8_t crc, const uint8_t * buf, int len)
{
	while(len--)
		crc = crc8_byte(crc, *buf++);
	return crc;
}
```

## 四字节小端模式转换函数

协议中的长度是指命令+数据+crc总字节数，不包含自身。

```c
uint32_t to_value32(uint8_t * buf)
{
	uint32_t val;

	val = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3] << 0);
	return val;
}

void to_buffer32(uint32_t val, uint8_t * buf)
{
	buf[0] = (val >> 24) & 0xff;
	buf[1] = (val >> 16) & 0xff;
	buf[2] = (val >>  8) & 0xff;
	buf[3] = (val >>  0) & 0xff;
}
```

## 通信命令，现在协议中定义了5个命令
```c
#define CMD_SET_BRIGHTNESS	(0x01)
#define CMD_CLEAR_SCREEN	(0x02)
#define CMD_BLIT_JPG		(0x03)
#define CMD_BLIT_PNG		(0x04)
#define CMD_REBOOT			(0x05)
#define CMD_REBOOT_TO_FEL	(0xfe)
```

| CMD_SET_BRIGHTNESS | 设置背光亮度，需传递亮度值，一个字节（0 - 255） |
| ------------------ | ----------------------------------------------- |
| CMD_CLEAR_SCREEN   | 清除屏幕至某个颜色，三个字节RGB，（R，G，B）    |
| CMD_BLIT_JPG       | 显示jpg图片                                     |
| CMD_BLIT_PNG       | 显示png图片                                     |
| CMD_REBOOT         | 系统重启                                        |
| CMD_REBOOT_TO_FEL  | 清除flash固件，重新烧录，注意，这条命令谨慎使用 |



## 每传输一帧需等待下位机的ack信号，相关参考代码如下


```c
static int ack = 0;

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
			if((serial_read_byte(fd, &c) == 1) && (c == 0x58))
			{
				ack = 1;
			}
		}
	}
	return 0;
}

void uart_clear_ack(void)
{
	ack = 0;
}

void uart_wait_ack(void)
{
	uint64_t time, end;

	end = get_current_time() + 1000;
	do {
		usleep(1);
		time = get_current_time();
	} while((ack == 0) && (time <= end));
}

```

## 底层帧发送函数
```c
void serial_sendcmd(uint8_t cmd, uint8_t * dat, int len)
{
	uint8_t header = 0x58;
	uint8_t length[4];
	uint8_t crc = 0;

	to_buffer32(1 + len + 1, &length[0]);
	crc = crc8(crc, &header, 1);
	crc = crc8(crc, &length[0], 4);
	crc = crc8(crc, &cmd, 1);
	if(dat && (len > 0))
		crc = crc8(crc, dat, len);

	uart_clear_ack();
	serial_write(fd, &header, 1);
	serial_write(fd, &length[0], 4);
	serial_write(fd, &cmd, 1);
	if(dat && (len > 0))
		serial_write(fd, dat, len);
	serial_write(fd, &crc, 1);
	uart_wait_ack();
}

```

## 设置亮度

```c
void infree_set_brightness(uint8_t brightness)
{
	serial_sendcmd(CMD_SET_BRIGHTNESS, &brightness, 1);
}


```

## 清除屏幕

```c

void infree_clear_screen(uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t rgb[3] = {r, g, b};
	serial_sendcmd(CMD_CLEAR_SCREEN, &rgb[0], 3);
}


```


## 显示jpg图片

```c

void infree_blit_jpg(uint8_t * jpg, int len)
{
	serial_sendcmd(CMD_BLIT_JPG, jpg, len);
}


```


## 显示png图片

```c

void infree_blit_png(uint8_t * png, int len)
{
	serial_sendcmd(CMD_BLIT_PNG, png, len);
}



```

## 系统重启

```c
void infree_reboot(void)
{
	uint8_t rst[3] = {'R', 'S', 'T'};
	serial_sendcmd(CMD_REBOOT, &rst[0], 3);
}


```


## 清除flash固件，重新烧录，注意，这条命令谨慎使用

```c

void infree_reboot_to_fel(void)
{
	uint8_t fel[3] = {'F', 'E', 'L'};
	serial_sendcmd(CMD_REBOOT_TO_FEL, &fel[0], 3);
}


```

