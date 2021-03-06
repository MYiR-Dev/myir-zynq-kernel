#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <asm/uaccess.h>
#include <linux/slab.h>
#include <linux/kdev_t.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <asm/gpio.h>
#include <linux/of.h>
#include <linux/of_gpio.h>

#define DEV_NAME		"myir-watchdog"
#define RESET_MS		400 /* Default reset period set to 400 ms */
#define DEFAULT_WDI		0 // PS_MIO0

struct watchdog_data {
	struct class class;
	struct timer_list timer;
	int period;
	int gpio;
	unsigned char gpio_value;
	unsigned char running;
};

static void reset_watchdog(struct timer_list *t)
{
	// struct watchdog_data * pdata = (struct watchdog_data *) data;
    struct watchdog_data * pdata = from_timer(pdata, t, timer);

	pdata->gpio_value ^= 0x1;
    gpio_direction_output(pdata->gpio, pdata->gpio_value);
	mod_timer(&pdata->timer, jiffies + msecs_to_jiffies(pdata->period));
	// printk(KERN_ALERT "- reset wd.\n");
}

/* Initialize hrtimer */
static void initialize_timer(struct watchdog_data * pdata)
{
	if(!pdata) {
		printk(KERN_ERR "Watchdog device has not been initialized yet.\n");
		return;
	}

	if (pdata->period <= 0) {
		pdata->period = RESET_MS;
	}

	// setup_timer( &pdata->timer, reset_watchdog, (unsigned long)pdata );
    timer_setup(&pdata->timer, reset_watchdog, 0);
}

/* Destroy hrtimer */
static void destroy_timer(struct watchdog_data * pdata)
{
	printk( KERN_ALERT "Watchdog timer destroy\n");
	if(pdata && pdata->running) {
		del_timer(&pdata->timer);
	}
}

/* class attribute show function. */
static ssize_t wd_period_ms_show(struct class *cls, struct class_attribute *attr, char *buf)
{
	struct watchdog_data *pdata = (struct watchdog_data *)container_of(cls, struct watchdog_data, class);
	return sprintf(buf, "%d\n", pdata->running?pdata->period:0);
}

/* class attribute store function. */
static ssize_t wd_period_ms_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t count)
{
	struct watchdog_data *pdata = (struct watchdog_data *)container_of(cls, struct watchdog_data, class);
	int tmp;
	
	if(!buf || sscanf(buf, "%d", &tmp) <= 0) {
		return -EINVAL;
	}
	
	if(tmp == 0 && pdata->running) { /* Stop the watchdog timer */
		del_timer(&pdata->timer);
		pdata->running = 0;
		
		/* Set gpio to input(High-Z state) to disable external watchdog timer */
		gpio_direction_input(pdata->gpio);
		
		printk("Cancel watchdog timer!\n");
	} else if(tmp > 0) {
		printk(KERN_ALERT "Set period to %d ms .\n", tmp);
		pdata->period = tmp;
		if(!pdata->running) {
			printk(KERN_ALERT "Start WD timer.\n");
			mod_timer(&pdata->timer, jiffies + msecs_to_jiffies(pdata->period));
			pdata->running = 1;
		}
	} else {
		return -EINVAL;
	}
	
	return count;
}
static CLASS_ATTR_RW(wd_period_ms);

static ssize_t wd_feed_show(struct class *cls, struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "write '1' to enable manual-mode and disable auto-mode.\n");
}

static ssize_t wd_feed_store(struct class *cls, struct class_attribute *attr, const char *buf, size_t count)
{
	struct watchdog_data *pdata = (struct watchdog_data *)container_of(cls, struct watchdog_data, class);
	int tmp;
	if(!buf || sscanf(buf, "%d", &tmp) <= 0) {
		return -EINVAL;
	}
	
	if(tmp == 0) {
		if(pdata->running == 0) {
			printk(KERN_ALERT "Cancel watchdog.\n");
			/* Set gpio to input(High-Z state) to disable external watchdog timer */
			gpio_direction_input(pdata->gpio);
		} else {
			printk(KERN_ALERT "Can not cancel watchdog by writing 'wd_feed' while running in auto-mode.\n");
		}
	} else if(tmp > 0) {
		if(pdata->running) {
			printk(KERN_ALERT "Disable auto-mode and switch to manual-mode.\n");
			del_timer(&pdata->timer);
			pdata->running = 0;
		}
		pdata->gpio_value ^= 0x1;
		gpio_direction_output(pdata->gpio, pdata->gpio_value);
	} else {
		return -EINVAL;
	}
	return count;
}
static CLASS_ATTR_RW(wd_feed);

static struct attribute *watchdog_class_attrs[] = {
	&class_attr_wd_period_ms.attr,
	&class_attr_wd_feed.attr,
	NULL,
};
ATTRIBUTE_GROUPS(watchdog_class);

//Module initialization.
static int watchdog_probe(struct platform_device *pdev)
{
	int ret = 0;
	u32 gpio = ARCH_NR_GPIOS;
	u32 timeout_ms = RESET_MS;
	struct watchdog_data * pdata;
	enum of_gpio_flags flags;
	
	pdata = kmalloc(sizeof(struct watchdog_data), GFP_KERNEL);
	if(!pdata) {
		printk(KERN_ERR "No memory!\n");
		return -ENOMEM;
	}
	memset(pdata, 0, sizeof(struct watchdog_data));
	/* Init gpio */	
	gpio = of_get_gpio_flags(pdev->dev.of_node, 0, &flags);
	if (!gpio_is_valid(gpio))
		printk(KERN_ERR "invalid gpio:%d\n", gpio);

	pdata->gpio = gpio;

	ret = gpio_request(pdata->gpio, DEV_NAME);
	if(ret < 0) {
		printk(KERN_ERR "request gpio %d for %s failed!, errno:%d\n", pdata->gpio, DEV_NAME, ret);
		goto gpio_request_fail;
	}
	/* init wdt feed interval */
	if (!of_property_read_u32(pdev->dev.of_node, "timeout-ms", &timeout_ms)) {
		pdata->period = timeout_ms;
	} else {
		pdata->period = RESET_MS; /* Init period */
	}
	
	initialize_timer(pdata);

	/* Init class */
	pdata->class.name = DEV_NAME;
	pdata->class.owner = THIS_MODULE;
	pdata->class.class_groups = watchdog_class_groups;
	ret = class_register(&pdata->class);
	// ret = class_register(&watchdog_class);
	if(ret) {
		printk(KERN_ERR "class_register failed!\n");
		goto class_register_fail;
	}
	
	/* Start watchdog timer here */
	gpio_direction_output(pdata->gpio, pdata->gpio_value);
	mod_timer(&pdata->timer, jiffies + msecs_to_jiffies(pdata->period));
	pdata->running = 1;
	
	printk(KERN_INFO "%s driver initialized successfully!\n", DEV_NAME);
	return 0;

class_register_fail:
	destroy_timer(pdata);
	gpio_free(pdata->gpio);
	
gpio_request_fail:
	kfree(pdata);
	
	return ret;
}

static int __exit watchdog_remove(struct platform_device *pdev)
{
	struct watchdog_data * dev = platform_get_drvdata(pdev);
	if(dev) {
		destroy_timer(dev);
		gpio_free(dev->gpio);
		class_unregister(&dev->class);
		kfree(dev);
	}

	return 0;
}

#ifdef CONFIG_PM
static int watchdog_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct watchdog_data * pdata = platform_get_drvdata(pdev);
	if(pdata) {
		gpio_direction_input(pdata->gpio);
		if(pdata->running) {
			del_timer(&pdata->timer);
		}
	}
	return 0;
}

static int watchdog_resume(struct platform_device *pdev)
{
	struct watchdog_data * pdata = platform_get_drvdata(pdev);
	if(pdata) {
		if(pdata->running) {
			mod_timer(&pdata->timer, jiffies + msecs_to_jiffies(pdata->period));
		}
	}
	return 0;
}
#else
	#define	watchdog_suspend	NULL
	#define	watchdog_resume		NULL
#endif

static struct of_device_id wdt_of_match[] = {
	{ .compatible = "myir_watchdog", },
	{ /* end of table */}
};
MODULE_DEVICE_TABLE(of, wdt_of_match);


static struct platform_driver watchdog_platfrom_driver = {
//	.probe   = watchdog_probe,
	.remove  = watchdog_remove,
	.suspend = watchdog_suspend,
	.resume  = watchdog_resume,
	.driver = {
		.name = DEV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = wdt_of_match,
	},
};

static int __init watchdog_init(void)
{
	return platform_driver_probe(&watchdog_platfrom_driver, watchdog_probe);
}

static void __exit watchdog_exit(void)
{
	platform_driver_unregister(&watchdog_platfrom_driver);
}

module_init(watchdog_init);
module_exit(watchdog_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Kevin Su <kevin.su@myirtech.com>");
MODULE_DESCRIPTION("MYIR Watch Dog Driver.");
