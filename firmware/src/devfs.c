#include <devfs/devfs.h>
#include <sys/types.h>
#include <termlet_fs.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>

static int uart_open(const struct devfs_entry *entry,
                     struct fs_file_t *file,
                     fs_mode_t flags) {

	const struct device *dev = entry->userdata;
	if (!device_is_ready(dev))
		return -ENODEV;

	return 0;
}

static ssize_t uart_write(const struct devfs_entry *entry,
                          struct fs_file_t *file,
                          const void *ptr,
                          size_t size) {
	ARG_UNUSED(file);

	const struct device *dev = entry->userdata;
	const uint8_t *buf = ptr;
	size_t written = 0;

	while (written < size) {
		uart_poll_out(dev, buf[written]);
		written++;
	}

	return (ssize_t) written;
}

static ssize_t uart_read(const struct devfs_entry *entry,
                         struct fs_file_t *file,
                         void *ptr,
                         size_t size)
{
	ARG_UNUSED(file);

	const struct device *dev = entry->userdata;
	uint8_t *buf = ptr;

	if (size == 0)
		return 0;

	size_t n = 0;

	/* Block until at least one byte arrives. */
	while (uart_poll_in(dev, &buf[n]) == -1)
		k_sleep(K_MSEC(1));

	n++;

	/* Drain all immediately available bytes. */
	while (n < size) {
		int ret = uart_poll_in(dev, &buf[n]);

		if (ret == -1)
			break;

		if (ret < 0)
			return n > 0 ? (ssize_t)n : ret;

		n++;
	}

	return (ssize_t)n;
}

static const struct devfs_ops uart_ops = {
	.open = uart_open,
	.write = uart_write,
	.read = uart_read,
};


const struct devfs_entry devices[] = {

#if DT_HAS_CHOSEN(zephyr_console)
#	define STDIO_DEVICE DEVICE_DT_GET(DT_CHOSEN(zephyr_console))
	{
	    .name = "stdin",
	    .ops = &uart_ops,
	    .userdata = (void *) STDIO_DEVICE,
	},
	{
	    .name = "stdout",
	    .ops = &uart_ops,
	    .userdata = (void *) STDIO_DEVICE,
	},
	{
	    .name = "stderr",
	    .ops = &uart_ops,
	    .userdata = (void *) STDIO_DEVICE,
	},
#endif

	{ 0 }
};
