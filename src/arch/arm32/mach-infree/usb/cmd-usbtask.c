/*
 * kernel/command/cmd-usbtask.c
 */

#include <xboot.h>
#include <usb.h>
#include <command/command.h>

static void usage(void)
{
	printf("usage:\r\n");
	printf("    usbtask [args ...]\r\n");
}

static int do_usbtask(int argc, char ** argv)
{
	usb_device_init(USB_TYPE_USB_COM);
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
