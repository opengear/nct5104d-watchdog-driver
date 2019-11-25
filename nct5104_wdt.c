#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define MODULE_NAME		"nct5104_wdt"

/* TIMEOUT is in seconds */
#define WATCHDOG_TIMEOUT	60
#define WATCHDOG_TIMEOUT_MAX	255

#define REG_CHIP_ID		0x20		/* Chip ID register */
#define NCT5104D_ID_REV_B	0xC452		/* Chip rev B ID */
#define NCT5104D_ID_REV_C	0xC453		/* Chip rev C ID */

#define REG_EN			0x2E		/* Enable Register */
#define REG_IDX			REG_EN		/* Index Register */
#define REG_VAL			(REG_IDX + 1)	/* Value / Data Register */

#define REG_LDN			0x07		/* Logical Device Register */

#define LDN_WDT			0x08		/* Watchdog Logical Device */
#define REG_WDT_STATUS		0x30		/* LDN 8 status */
#define REG_WDT_CNTR_MODE	0xF0		/* WDT Counter Mode */
#define REG_WDT_CNTR_VALUE	0xF1		/* WDT Counter Value */
#define REG_WDT_CTRL_STATUS	0xF2		/* WDT Control & Status */


static int superio_enter(void)
{
	/* Reserve IO Addresses for exclusive access. */
	if (!request_muxed_region(REG_EN, 2, MODULE_NAME)) {
		pr_debug("%s could not enter region\n", __func__);
		return -EBUSY;
	}

	/* enter Exteded Function Mode; 0x87 is the magic config key */
	outb(0x87, REG_EN);
	outb(0x87, REG_EN);
	return 0;
}

static void superio_exit(void)
{
	/* exit Extended Function Mode */
        outb(0xAA, REG_EN);
        release_region(REG_EN, 2);
}

static int superio_inb(int reg)
{
        outb(reg, REG_IDX);
        return inb(REG_VAL);
}

static void superio_outb(int val, int reg)
{
        outb(reg, REG_IDX);
        outb(val, REG_VAL);
}

static int superio_inw(int reg)
{
        int val;
        outb(reg++, REG_IDX);
        val = inb(REG_VAL) << 8;
        outb(reg, REG_IDX);
        val |= inb(REG_VAL);
        return val;
}

static inline void superio_select(int ldn)
{
	superio_outb(ldn, REG_LDN);
}

static inline void wdt_select(void)
{
	superio_select(LDN_WDT);
}

/* Set new watchdog timeout value in seconds */
static int wdt_set_timeout(struct watchdog_device *wdev, unsigned t)
{
	int ret;
	pr_debug("%s\n", __func__);

	/* FIXME: check / convert units... */
	if (t < wdev->min_timeout || t > wdev->max_timeout) {
		pr_err("invalid watchdog timeout (%u) ignored.\n", t);
		return -EINVAL;
	}

	ret = superio_enter();
	if (ret)
		return ret;

	wdt_select();
	superio_outb(t, REG_WDT_CNTR_VALUE);
	superio_exit();

	pr_debug("timeout set to %d seconds.\n", t);
	wdev->timeout = t;

	return 0;
}

static int wdt_start(struct watchdog_device *wdev)
{
	int ret;
	u8 reg;

	pr_debug("%s\n", __func__);

	ret = superio_enter();
	if (ret)
		return ret;

	wdt_select();

	pr_debug("wdt status before start: 0x%02x, control & status: 0x%02x, counter mode: 0x%02x, time left: %ds\n",
		 superio_inb(REG_WDT_STATUS),
		 superio_inb(REG_WDT_CTRL_STATUS),
		 superio_inb(REG_WDT_CNTR_MODE),
		 superio_inb(REG_WDT_CNTR_VALUE));

	/* clear the Time-out event status bit */
	reg = superio_inb(REG_WDT_CTRL_STATUS);
	reg &= ~BIT(4);
	superio_outb(reg, REG_WDT_CTRL_STATUS);

	/* set the timeout counter */
	superio_outb(wdev->timeout, REG_WDT_CNTR_VALUE);

	/* set WDT active */
	reg = superio_inb(REG_WDT_STATUS);
	reg |= 1;
	superio_outb(reg, REG_WDT_STATUS);

	pr_debug("wdt status: 0x%02x, control & status: 0x%02x, counter mode: 0x%02x, time left: %ds\n",
		 superio_inb(REG_WDT_STATUS),
		 superio_inb(REG_WDT_CTRL_STATUS),
		 superio_inb(REG_WDT_CNTR_MODE),
		 superio_inb(REG_WDT_CNTR_VALUE));
	pr_debug("wdt superio logical device selection was 0x%02x, should be 0x%02x\n",
		 superio_inb(REG_LDN), LDN_WDT);

	/* sanity check for misbehaving chip */
	reg = superio_inb(REG_WDT_CNTR_MODE);
	if (reg & BIT(0))
		pr_err(MODULE_NAME ": control register read non-zero in reserved bit\n");

	superio_exit();

	return 0;
}

static int wdt_stop(struct watchdog_device *wdev)
{
	int ret;

	pr_debug("%s\n", __func__);

	ret = superio_enter();
	if (ret)
		return ret;

	wdt_select();
	superio_outb(0, REG_WDT_CNTR_VALUE);
	superio_exit();

	return 0;
}

static unsigned int wdt_get_timeleft(struct watchdog_device *wdev)
{
	int ret, timeleft;

	pr_debug("%s\n", __func__);

	ret = superio_enter();
	if (ret) {
		/*
		 * BUG? :(
		 * no way to return error code to caller
		 * watchdog_get_timeleft()
		 */
		pr_err("Driver can't access hw.\n");
		return 0;
	}

	wdt_select();
	timeleft = superio_inb(REG_WDT_CNTR_VALUE);
	pr_debug("control & status: 0x%02x, counter mode: 0x%02x, time left: %ds\n",
		 superio_inb(REG_WDT_CTRL_STATUS),
		 superio_inb(REG_WDT_CNTR_MODE),
		 timeleft);
	superio_exit();

	return timeleft;
}

static const struct watchdog_ops wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.set_timeout = wdt_set_timeout,
	.get_timeleft = wdt_get_timeleft,
};

/* We want to use magicclose, but we need the userspace tool first. */
static const struct watchdog_info wdt_info = {
	.options = WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE | WDIOF_SETTIMEOUT,
	.identity = MODULE_NAME
};

static struct watchdog_device wdd = {
	.info = &wdt_info,
	.ops = &wdt_ops,
	.timeout = WATCHDOG_TIMEOUT,
	.min_timeout = 1,
	.max_timeout = WATCHDOG_TIMEOUT_MAX,
};

static int wdt_probe(void)
{
	int ret;
	int chip_id;

	ret = superio_enter();
	if (ret)
		return ret;

	chip_id = superio_inw(REG_CHIP_ID);
	pr_debug("Got chip id: 0x%04x\n", chip_id);

	ret = -ENODEV;
	switch (chip_id) {
	case NCT5104D_ID_REV_B:
	case NCT5104D_ID_REV_C:
		/* matched */
		ret = 0;
		break;
	case 0xffff:
		pr_err(MODULE_NAME ": chip ID register returned 0x%04x, hardware fault?\n", chip_id);
		break;
	default:
		/* return -ENODEV */
		break;
	}

	superio_exit();
	return ret;
}

/* TODO: fix assumption that base address is strapped to 0x2E */
static int __init wdt_platform_probe(struct platform_device *pdev)
{
	int ret;
	u8 reg;

	dev_dbg(&pdev->dev, "%s\n", __func__);

	ret = wdt_probe();
	if (ret)
		return ret;

	ret = superio_enter();
	if (ret)
		return ret;

	wdt_select();

	/* deactivate WDT */
	reg = superio_inb(REG_WDT_STATUS);
	reg &= ~BIT(0);
	superio_outb(reg, REG_WDT_STATUS);

	/* set the timeout counter mode to seconds */
	reg = superio_inb(REG_WDT_CNTR_MODE);
	reg &= ~BIT(3) & ~BIT(4);
	superio_outb(reg, REG_WDT_CNTR_MODE);

	/* clear the Time-out event status bit */
	reg = superio_inb(REG_WDT_CTRL_STATUS);
	reg &= ~BIT(4);
	superio_outb(reg, REG_WDT_CTRL_STATUS);

	/* set the timeout counter to max */
	superio_outb(0xFF, REG_WDT_CNTR_VALUE);

	/* (re)activate WDT */
	reg = superio_inb(REG_WDT_STATUS);
	reg |= BIT(0);
	superio_outb(reg, REG_WDT_STATUS);

	/* set the timeout counter to 32s */
	superio_outb(0x20, REG_WDT_CNTR_VALUE);

	pr_debug("wdt status: 0x%02x, control & status: 0x%02x, counter mode: 0x%02x, time left: %ds\n",
		 superio_inb(REG_WDT_STATUS),
		 superio_inb(REG_WDT_CTRL_STATUS),
		 superio_inb(REG_WDT_CNTR_MODE),
		 superio_inb(REG_WDT_CNTR_VALUE));

	superio_exit();

	ret = watchdog_register_device(&wdd);
	if (ret) {
		pr_err("Cannot register watchdog device. Err=%d\n", ret);
		return ret;
	}

	pr_info("NCT5104D watchdog initialised.\n");

	return 0;
}

static const struct of_device_id wdt_dt_ids[] = {
        { .compatible = "onnn,nct5104d-wdt", },
        { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, wdt_dt_ids);

static struct platform_driver wdt_driver = {
	.driver		= {
		.name	= MODULE_NAME,
		.of_match_table	= wdt_dt_ids
	},
};
module_platform_driver_probe(wdt_driver, wdt_platform_probe);

MODULE_DESCRIPTION("Watchdog Device Driver for NCT5104D LPC SuperIO");
MODULE_LICENSE("GPL");
