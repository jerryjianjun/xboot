/*
 * kernel/command/cmd-usbtask.c
 */

#include <xboot.h>
#include <usb.h>
#include <png.h>
#include <pngstruct.h>
#include <jpeglib.h>
#include <jerror.h>
#include <watchdog/watchdog.h>
#include <command/command.h>

struct x_error_mgr
{
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

struct x_source_mgr
{
	struct jpeg_source_mgr pub;
	struct xfs_file_t * file;
	JOCTET * buffer;
	int start_of_file;
};

static void x_error_exit(j_common_ptr dinfo)
{
	struct x_error_mgr * err = (struct x_error_mgr *)dinfo->err;
	(*dinfo->err->output_message)(dinfo);
	longjmp(err->setjmp_buffer, 1);
}

static void x_emit_message(j_common_ptr dinfo, int msg_level)
{
	struct jpeg_error_mgr * err = dinfo->err;
	if(msg_level < 0)
		err->num_warnings++;
}

static struct surface_t * surface_alloc_from_buffer_jpg(uint8_t * buffer, size_t len)
{
    struct jpeg_decompress_struct dinfo;
    struct x_error_mgr jerr;
    struct surface_t * s;
	JSAMPARRAY buf;
	unsigned char * p;
	int scanline, offset, i;

	dinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = x_error_exit;
	jerr.pub.emit_message = x_emit_message;
	if(setjmp(jerr.setjmp_buffer))
	{
		jpeg_destroy_decompress(&dinfo);
		return NULL;
	}
	jpeg_create_decompress(&dinfo);
    jpeg_mem_src(&dinfo, buffer, len);
	jpeg_read_header(&dinfo, 1);
	jpeg_start_decompress(&dinfo);
	buf = (*dinfo.mem->alloc_sarray)((j_common_ptr)&dinfo, JPOOL_IMAGE, dinfo.output_width * dinfo.output_components, 1);
	s = surface_alloc(dinfo.image_width, dinfo.image_height, NULL);
	p = surface_get_pixels(s);
	while(dinfo.output_scanline < dinfo.output_height)
	{
		scanline = dinfo.output_scanline * surface_get_stride(s);
		jpeg_read_scanlines(&dinfo, buf, 1);
		for(i = 0; i < dinfo.output_width; i++)
		{
			offset = scanline + (i * 4);
			p[offset + 3] = 0xff;
			p[offset + 2] = buf[0][(i * 3) + 0];
			p[offset + 1] = buf[0][(i * 3) + 1];
			p[offset + 0] = buf[0][(i * 3) + 2];
		}
	}
	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);

	return s;
}

static inline int multiply_alpha(int alpha, int color)
{
	int temp = (alpha * color) + 0x80;
	return ((temp + (temp >> 8)) >> 8);
}

static void premultiply_data(png_structp png, png_row_infop row_info, png_bytep data)
{
	unsigned int i;

	for(i = 0; i < row_info->rowbytes; i += 4)
	{
		uint8_t * base = &data[i];
		uint8_t alpha = base[3];
		uint32_t p;

		if(alpha == 0)
		{
			p = 0;
		}
		else
		{
			uint8_t red = base[0];
			uint8_t green = base[1];
			uint8_t blue = base[2];

			if(alpha != 0xff)
			{
				red = multiply_alpha(alpha, red);
				green = multiply_alpha(alpha, green);
				blue = multiply_alpha(alpha, blue);
			}
			p = (alpha << 24) | (red << 16) | (green << 8) | (blue << 0);
		}
		memcpy(base, &p, sizeof(uint32_t));
	}
}

static void convert_bytes_to_data(png_structp png, png_row_infop row_info, png_bytep data)
{
	unsigned int i;

	for(i = 0; i < row_info->rowbytes; i += 4)
	{
		uint8_t * base = &data[i];
		uint8_t red = base[0];
		uint8_t green = base[1];
		uint8_t blue = base[2];
		uint32_t pixel;

		pixel = (0xff << 24) | (red << 16) | (green << 8) | (blue << 0);
		memcpy(base, &pixel, sizeof(uint32_t));
	}
}

struct png_source_t {
	unsigned char * data;
	int size;
	int offset;
};

static void png_source_read_data(png_structp png, png_bytep data, png_size_t length)
{
	struct png_source_t * src = (struct png_source_t *)png_get_io_ptr(png);

	if(src->offset + length <= src->size)
	{
		memcpy(data, src->data + src->offset, length);
		src->offset += length;
	}
	else
		png_error(png, "read error");
}

static struct surface_t * surface_alloc_from_buffer_png(uint8_t * buffer, size_t len)
{
	struct surface_t * s;
	png_struct * png;
	png_info * info;
	png_byte * data = NULL;
	png_byte ** row_pointers = NULL;
	png_uint_32 png_width, png_height;
	int depth, color_type, interlace, stride;
	unsigned int i;

	png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	if(!png)
		return NULL;

	info = png_create_info_struct(png);
	if(!info)
	{
		png_destroy_read_struct(&png, NULL, NULL);
		return NULL;
	}

	struct png_source_t src;
	src.data = buffer;
	src.size = len;
	src.offset = 0;
	png_set_read_fn(png, &src, png_source_read_data);

#ifdef PNG_SETJMP_SUPPORTED
	if(setjmp(png_jmpbuf(png)))
	{
		png_destroy_read_struct(&png, &info, NULL);
		return NULL;
	}
#endif

	png_read_info(png, info);
	png_get_IHDR(png, info, &png_width, &png_height, &depth, &color_type, &interlace, NULL, NULL);

	if(color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_palette_to_rgb(png);
	if(color_type == PNG_COLOR_TYPE_GRAY)
	{
#if PNG_LIBPNG_VER >= 10209
		png_set_expand_gray_1_2_4_to_8(png);
#else
		png_set_gray_1_2_4_to_8(png);
#endif
	}
	if(png_get_valid(png, info, PNG_INFO_tRNS))
		png_set_tRNS_to_alpha(png);
	if(depth == 16)
		png_set_strip_16(png);
	if(depth < 8)
		png_set_packing(png);
	if(color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
		png_set_gray_to_rgb(png);
	if(interlace != PNG_INTERLACE_NONE)
		png_set_interlace_handling(png);

	png_set_filler(png, 0xff, PNG_FILLER_AFTER);
	png_read_update_info(png, info);
	png_get_IHDR(png, info, &png_width, &png_height, &depth, &color_type, &interlace, NULL, NULL);

	if(depth != 8 || !(color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_RGB_ALPHA))
	{
		png_destroy_read_struct(&png, &info, NULL);
		return NULL;
	}

	switch(color_type)
	{
	case PNG_COLOR_TYPE_RGB_ALPHA:
		png_set_read_user_transform_fn(png, premultiply_data);
		break;
	case PNG_COLOR_TYPE_RGB:
		png_set_read_user_transform_fn(png, convert_bytes_to_data);
		break;
	default:
		break;
	}

	s = surface_alloc(png_width, png_height, NULL);
	data = surface_get_pixels(s);

	row_pointers = (png_byte **)malloc(png_height * sizeof(char *));
	stride = png_width * 4;

	for(i = 0; i < png_height; i++)
		row_pointers[i] = &data[i * stride];

	png_read_image(png, row_pointers);
	png_read_end(png, info);
	free(row_pointers);
	png_destroy_read_struct(&png, &info, NULL);

	return s;
}


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

/*
 * value and buffer translate
 */
static uint32_t to_value32(uint8_t * buf)
{
	uint32_t val;

	val = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | (buf[3] << 0);
	return val;
}

enum com_state_t {
	COM_STATE_HEADER	= 0,
	COM_STATE_LENGTH0	= 1,
	COM_STATE_LENGTH1	= 2,
	COM_STATE_LENGTH2	= 3,
	COM_STATE_LENGTH3	= 4,
	COM_STATE_COMMAND	= 5,
	COM_STATE_DATA		= 6,
	COM_STATE_CRC		= 7,
};

#define CMD_SET_BRIGHTNESS	(0x01)
#define CMD_CLEAR_SCREEN	(0x02)
#define CMD_BLIT_JPG		(0x03)
#define CMD_BLIT_PNG		(0x04)
#define CMD_REBOOT_TO_FEL	(0xfe)

static struct framebuffer_t * fb = NULL;
static struct surface_t * screen = NULL;
static uint8_t * buffer = NULL;
static struct fifo_t * fifo = NULL;

extern int usb_device_write_data(int ep,unsigned char * databuf,int len);
void com_ack(void)
{
	uint8_t ack = 0x58;
	usb_device_write_data(1, &ack, 1);
}

void com_receive(uint8_t c)
{
	static enum com_state_t state = COM_STATE_HEADER;
	static uint32_t index = 0;
	uint32_t len, crc;

	buffer[index++] = c;
	switch(state)
	{
	case COM_STATE_HEADER:
		if(c == 0x58)
		{
			state = COM_STATE_LENGTH0;
		}
		else
		{
			index = 0;
			state = COM_STATE_HEADER;
		}
		break;

	case COM_STATE_LENGTH0:
		state = COM_STATE_LENGTH1;
		break;

	case COM_STATE_LENGTH1:
		state = COM_STATE_LENGTH2;
		break;

	case COM_STATE_LENGTH2:
		state = COM_STATE_LENGTH3;
		break;

	case COM_STATE_LENGTH3:
		len = to_value32(&buffer[1]);
		if((len > 2) && (len < SZ_2M - 5))
		{
			state = COM_STATE_COMMAND;
		}
		else
		{
			index = 0;
			state = COM_STATE_HEADER;
		}
		break;

	case COM_STATE_COMMAND:
		state = COM_STATE_DATA;
		break;

	case COM_STATE_DATA:
		len = to_value32(&buffer[1]);
		if(index >= len + 4)
			state = COM_STATE_CRC;
		break;

	case COM_STATE_CRC:
		len = to_value32(&buffer[1]);
		crc = crc8(0, buffer, len + 4);
		if(crc == c)
			fifo_put(fifo, &buffer[0], len + 5);
        index = 0;
		state = COM_STATE_HEADER;
		break;

	default:
        index = 0;
		state = COM_STATE_HEADER;
		break;
	}
}

static void usage(void)
{
	printf("usage:\r\n");
	printf("    usbtask [args ...]\r\n");
}

static uint8_t tmp[SZ_2M];

void usb_task(struct task_t * task, void * data)
{
	struct watchdog_t * wdg = search_first_watchdog();
	uint8_t hbuf[5];
	uint32_t len;

	if(!buffer)
		buffer = malloc(SZ_2M);
	if(!fifo)
		fifo = fifo_alloc(SZ_4M);
	if(!fb)
	{
		fb = search_first_framebuffer();
		screen = framebuffer_create_surface(fb);
	}
	usb_device_init(USB_TYPE_USB_COM);

	while(1)
	{
		if(fifo_get(fifo, hbuf, 5) == 5)
		{
			len = to_value32(&hbuf[1]);
			if((len >= 2) && (len < SZ_2M))
			{
				if(fifo_get(fifo, tmp, len) == len)
				{
					switch(tmp[0])
					{
					case CMD_SET_BRIGHTNESS:
						{
							framebuffer_set_backlight(fb, (int)tmp[1] * 1000 / 255);
							com_ack();
						}
						break;

					case CMD_CLEAR_SCREEN:
						{
							struct color_t col;
							color_init(&col, tmp[1], tmp[2], tmp[3], 0xff);
							surface_clear(screen, &col, 0, 0, 0, 0);
							framebuffer_present_surface(fb, screen, NULL);
							com_ack();
						}
						break;

					case CMD_BLIT_JPG:
						{
							struct surface_t * jpg = surface_alloc_from_buffer_jpg(&tmp[1], len - 2);
							if(jpg)
							{
								struct matrix_t m;
								matrix_init_identity(&m);
								matrix_init_translate(&m, (surface_get_width(screen) - surface_get_width(jpg)) / 2, (surface_get_height(screen) - surface_get_height(jpg)) / 2);
								surface_blit(screen, NULL, &m, jpg, RENDER_TYPE_GOOD);
								framebuffer_present_surface(fb, screen, NULL);
								surface_free(jpg);
								com_ack();
							}
						}
						break;

					case CMD_BLIT_PNG:
						{
							struct surface_t * png = surface_alloc_from_buffer_png(&tmp[1], len - 2);
							if(png)
							{
								struct matrix_t m;
								matrix_init_identity(&m);
								matrix_init_translate(&m, (surface_get_width(screen) - surface_get_width(png)) / 2, (surface_get_height(screen) - surface_get_height(png)) / 2);
								surface_blit(screen, NULL, &m, png, RENDER_TYPE_GOOD);
								framebuffer_present_surface(fb, screen, NULL);
								surface_free(png);
								com_ack();
							}
						}
						break;

					case CMD_REBOOT_TO_FEL:
						{
							if((tmp[1] == 'F') && (tmp[2] == 'E') && (tmp[3] == 'L'))
							{
								com_ack();
								machine_reboot();
								mdelay(5000);
							}
						}
						break;

					default:
						break;
					}
				}
			}
			else
			{
				fifo_reset(fifo);
			}
		}
		//watchdog_set_timeout(wdg, 10);
		task_yield();
	}
}

static int do_usbtask(int argc, char ** argv)
{
	task_resume(task_create(scheduler_self(), "usbtask", usb_task, NULL, 0, 0));
	return 0;
}

static struct command_t cmd_usbtask = {
	.name	= "usbtask",
	.desc	= "usb task programmer",
	.usage	= usage,
	.exec	= do_usbtask,
};

static __init void usbtask_cmd_init(void)
{
	register_command(&cmd_usbtask);
}

static __exit void usbtask_cmd_exit(void)
{
	unregister_command(&cmd_usbtask);
}

command_initcall(usbtask_cmd_init);
command_exitcall(usbtask_cmd_exit);
