/*
 * arch/arm/mach-kirkwood/nsa-310-setup.c
 *
 * ZyXEL NSA-310 Setup
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>
#include <mach/kirkwood.h>
#include "common.h"
#include "mpp.h"

#define NSA310_GPIO_USB_POWER_OFF	21
#define NSA310_GPIO_POWER_OFF		48

static unsigned int nsa310_mpp_config[] __initdata = {
	MPP12_GPIO, /* led esata green */
	MPP13_GPIO, /* led esata red */
	MPP15_GPIO, /* led usb green */
	MPP16_GPIO, /* led usb red */
	MPP21_GPIO, /* control usb power off */
	MPP28_GPIO, /* led sys green */
	MPP29_GPIO, /* led sys red */
	MPP36_GPIO, /* key reset */
	MPP37_GPIO, /* key copy */
	MPP39_GPIO, /* led copy green */
	MPP40_GPIO, /* led copy red */
	MPP41_GPIO, /* led hdd green */
	MPP42_GPIO, /* led hdd red */
	MPP44_GPIO, /* ?? */
	MPP46_GPIO, /* key power */
	MPP48_GPIO, /* control power off */
	0
};

static struct i2c_board_info __initdata nsa310_i2c_info[] = {
	{ I2C_BOARD_INFO("adt7476", 0x2e) },
};

static void nsa310_power_off(void)
{
	gpio_set_value(NSA310_GPIO_POWER_OFF, 1);
}

static int __init nsa310_gpio_request(unsigned int gpio, unsigned long flags,
				       const char *label)
{
	int err;

	err = gpio_request_one(gpio, flags, label);
	if (err)
		pr_err("NSA-310: can't setup GPIO%u (%s), err=%d\n",
			gpio, label, err);

	return err;
}

static void __init nsa310_gpio_init(void)
{
	int err;

	err = nsa310_gpio_request(NSA310_GPIO_POWER_OFF, GPIOF_OUT_INIT_LOW,
				  "Power Off");
	if (!err)
		pm_power_off = nsa310_power_off;

	nsa310_gpio_request(NSA310_GPIO_USB_POWER_OFF, GPIOF_OUT_INIT_LOW,
			    "USB Power Off");
}

void __init nsa310_init(void)
{
	u32 dev, rev;

	kirkwood_mpp_conf(nsa310_mpp_config);

	nsa310_gpio_init();

	kirkwood_pcie_id(&dev, &rev);

	i2c_register_board_info(0, ARRAY_AND_SIZE(nsa310_i2c_info));
}

static int __init nsa310_pci_init(void)
{
	if (of_machine_is_compatible("zyxel,nsa310"))
		kirkwood_pcie_init(KW_PCIE0);

	return 0;
}

subsys_initcall(nsa310_pci_init);
