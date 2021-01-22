/* Copyright (c) 2012, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <media/rc-core.h>
#include <linux/platform_data/media/gpio-ir-recv.h>

#define GPIO_IR_DRIVER_NAME	"gpio-rc-recv"
#define GPIO_IR_DEVICE_NAME	"gpio_ir_recv"

#define KEY_FOCUSIN  				KEY_Q
#define KEY_FOCUSOUT 				KEY_W
#define KEY_AUTOFOCUS				KEY_E
#define KEY_MANUALFOCUS				KEY_R
#define KEY_HIKMENU					KEY_T
#define KEY_LENSMENU				KEY_Y
#define KEY_ROLLOVER				KEY_U	
#define KEY_SET						KEY_I
#define KEY_CLEAN					KEY_O
#define KEY_RUN						KEY_P

struct gpio_rc_dev {
	struct rc_dev *rcdev;
	int gpio_nr;
	bool active_low;
	struct timer_list flush_timer;
};

static struct rc_map_table hx1838_carmp3[] = {
	{0x2292, KEY_1 /*1*/						},
    {0x2293, KEY_2 /*2*/						},
    {0x22CC, KEY_3 /*3*/						},
    {0x228E, KEY_4 /*4*/						},
    {0x228F, KEY_5 /*5*/						},
    {0x22C8, KEY_6 /*6*/						},
    {0x228A, KEY_7 /*7*/						},
    {0x228B, KEY_8 /*8*/						},
    {0x22C4, KEY_9 /*9*/						},
    {0x22CA, KEY_UP                           },
    {0x22D2, KEY_DOWN                         },
    {0x2299, KEY_LEFT                         },
    {0x22C1, KEY_RIGHT                        },
	{0x2285, KEY_ZOOMIN /*zoom in*/			},
	{0x2286, KEY_ZOOMOUT /*zoom out*/			},
	{0x2280, KEY_FOCUSIN /*focus in*/			},
	{0x2281, KEY_FOCUSOUT /*focus out*/		},
	{0x22da, KEY_SET /*set*/					},
	{0x22c0, KEY_CLEAN /*clean*/				},
	{0x2287, KEY_RUN /*run*/					},
	{0x22dd, KEY_AUTOFOCUS /*auto focus*/		},
	{0x228c, KEY_MANUALFOCUS /*manual focus*/	},
    {0x22ce, KEY_ENTER                        }
};

static struct rc_map_list hx1838_carmp3_map = {
	.map = {
		.scan		= hx1838_carmp3,
		.size		= ARRAY_SIZE(hx1838_carmp3),
		.rc_type	= RC_TYPE_NEC,
		.name		= "rc-hx18380-carmp3",
	}
};
	

#ifdef CONFIG_OF
/*
 * Translate OpenFirmware node properties into platform_data
 */
static int gpio_ir_recv_get_devtree_pdata(struct device *dev,
				  struct gpio_ir_recv_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	enum of_gpio_flags flags;
	int gpio;
	int rval = 0;
	u64 allowed_protos = 0;

	gpio = of_get_gpio_flags(np, 0, &flags);
	if (gpio < 0) {
		if (gpio != -EPROBE_DEFER)
			dev_err(dev, "Failed to get gpio flags (%d)\n", gpio);
		return gpio;
	}

	pdata->gpio_nr = gpio;
	//pdata->active_low = (flags & OF_GPIO_ACTIVE_LOW);
	pdata->active_low = of_property_read_bool(np, "active_low");
	/* probe() takes care of map_name == NULL or allowed_protos == 0 */
	pdata->map_name = of_get_property(np, "linux,rc-map-name", NULL);
	pdata->allowed_protos = 0;
	//rval = of_property_read_u64(np, "allowed_protos", &allowed_protos);
	//pdata->allowed_protos = allowed_protos;

	return rval;
	
}

static const struct of_device_id gpio_ir_recv_of_match[] = {
	{ .compatible = "gpio-ir-receiver", },
	{ },
};
MODULE_DEVICE_TABLE(of, gpio_ir_recv_of_match);

#else /* !CONFIG_OF */

#define gpio_ir_recv_get_devtree_pdata(dev, pdata)	(-ENOSYS)

#endif

static irqreturn_t gpio_ir_recv_irq(int irq, void *dev_id)
{
	struct gpio_rc_dev *gpio_dev = dev_id;
	int gval;
	int rc = 0;
	enum raw_event_type type = IR_SPACE;

	gval = gpio_get_value(gpio_dev->gpio_nr);
	//printk("gpio_ir_recv_irq --> val %d\n",gval);
	if (gval < 0)
		goto err_get_value;

	//pr_err("---->[%s:%d] gval:%d\n", __func__, __LINE__, gval);
	/* 
		active_low =1
		将中断脉冲低电平沿作为IR_PULSE处理，脉冲高电平沿作为IR_SPACE 处理
		IR_SPACE 作为数据识别
	*/
	if (gpio_dev->active_low)
		gval = !gval;
	
	if (gval == 1)
		type = IR_PULSE;

	rc = ir_raw_event_store_edge(gpio_dev->rcdev, type);
	if (rc < 0)
		goto err_get_value;

	mod_timer(&gpio_dev->flush_timer,
		  jiffies + nsecs_to_jiffies(gpio_dev->rcdev->timeout));

	ir_raw_event_handle(gpio_dev->rcdev);

err_get_value:
	return IRQ_HANDLED;
}

static void flush_timer(unsigned long arg)
{
	struct gpio_rc_dev *gpio_dev = (struct gpio_rc_dev *)arg;
	DEFINE_IR_RAW_EVENT(ev);

	ev.timeout = true;
	ev.duration = gpio_dev->rcdev->timeout;
	ir_raw_event_store(gpio_dev->rcdev, &ev);
	ir_raw_event_handle(gpio_dev->rcdev);
}

static int gpio_ir_recv_probe(struct platform_device *pdev)
{
	struct gpio_rc_dev *gpio_dev;
	struct rc_dev *rcdev;
	const struct gpio_ir_recv_platform_data *pdata =
					pdev->dev.platform_data;
	int rc;

	if (pdev->dev.of_node) {
		struct gpio_ir_recv_platform_data *dtpdata =
			devm_kzalloc(&pdev->dev, sizeof(*dtpdata), GFP_KERNEL);
		if (!dtpdata)
			return -ENOMEM;
		rc = gpio_ir_recv_get_devtree_pdata(&pdev->dev, dtpdata);
		if (rc)
			return rc;
		pdata = dtpdata;
	}

	if (!pdata)
		return -EINVAL;

	if (pdata->gpio_nr < 0)
		return -EINVAL;

	gpio_dev = kzalloc(sizeof(struct gpio_rc_dev), GFP_KERNEL);
	if (!gpio_dev)
		return -ENOMEM;

	rcdev = rc_allocate_device();
	if (!rcdev) {
		rc = -ENOMEM;
		goto err_allocate_device;
	}

	rcdev->priv = gpio_dev;
	rcdev->driver_type = RC_DRIVER_IR_RAW;
	rcdev->input_name = GPIO_IR_DEVICE_NAME;
	rcdev->input_phys = GPIO_IR_DEVICE_NAME "/input0";
	rcdev->input_id.bustype = BUS_HOST;
	rcdev->input_id.vendor = 0x0001;
	rcdev->input_id.product = 0x0001;
	rcdev->input_id.version = 0x0100;
	rcdev->dev.parent = &pdev->dev;
	rcdev->driver_name = GPIO_IR_DRIVER_NAME;
	rcdev->min_timeout = 0;
	rcdev->timeout = IR_DEFAULT_TIMEOUT;
	rcdev->max_timeout = 10 * IR_DEFAULT_TIMEOUT;
	
	if (pdata->allowed_protos)
		rcdev->allowed_protocols = pdata->allowed_protos;
	else
		rcdev->allowed_protocols = RC_BIT_ALL;
	
	/* 使能nec, 添加自定义键盘 */
	rcdev->enabled_protocols = RC_BIT_NEC;
	rcdev->map_name = pdata->map_name ? pdata->map_name : RC_MAP_EMPTY;

	gpio_dev->rcdev = rcdev;
	gpio_dev->gpio_nr = pdata->gpio_nr;
	gpio_dev->active_low = pdata->active_low;
	
	setup_timer(&gpio_dev->flush_timer, flush_timer,
		    (unsigned long)gpio_dev);

	rc = gpio_request(pdata->gpio_nr, "gpio-ir-recv");
	if (rc < 0)
		goto err_gpio_request;
	rc  = gpio_direction_input(pdata->gpio_nr);
	if (rc < 0)
		goto err_gpio_direction_input;

	//Add by wwp
	rc =  rc_map_register(&hx1838_carmp3_map);
	if (rc < 0)
		goto err_request_irq;
	
	//pr_err("----->%s:%d rcdev->enabled_protocols:%llu\n", __func__, __LINE__, rcdev->enabled_protocols);
	rc = rc_register_device(rcdev);
	if (rc < 0) {
		dev_err(&pdev->dev, "failed to register rc device\n");
		goto err_register_rc_device;
	}
	//pr_err("----->%s:%d rcdev->enabled_protocols:%llu\n", __func__, __LINE__, rcdev->enabled_protocols);
	platform_set_drvdata(pdev, gpio_dev);

	rc = request_any_context_irq(gpio_to_irq(pdata->gpio_nr),
				gpio_ir_recv_irq,
			IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING,
					"gpio-ir-recv-irq", gpio_dev);
	//pr_err("----->%s:%d gpio_nr:%d allowed_protocols:%llu enabled_protocols:%llu active_low:%d rcdev->map_name:%s\n", 
	//		__func__, __LINE__,pdata->gpio_nr, rcdev->allowed_protocols,rcdev->enabled_protocols, gpio_dev->active_low, rcdev->map_name);
	if (rc < 0)
		goto err_request_irq;

	
	
	return 0;

err_request_irq:
	rc_unregister_device(rcdev);
	rcdev = NULL;
err_register_rc_device:
err_gpio_direction_input:
	gpio_free(pdata->gpio_nr);
err_gpio_request:
	rc_free_device(rcdev);
err_allocate_device:
	kfree(gpio_dev);
	return rc;
}

static int gpio_ir_recv_remove(struct platform_device *pdev)
{
	struct gpio_rc_dev *gpio_dev = platform_get_drvdata(pdev);

	free_irq(gpio_to_irq(gpio_dev->gpio_nr), gpio_dev);
	del_timer_sync(&gpio_dev->flush_timer);
	rc_unregister_device(gpio_dev->rcdev);
	gpio_free(gpio_dev->gpio_nr);
	kfree(gpio_dev);
	//Add by wwwwp
	rc_map_unregister(&hx1838_carmp3_map);
	return 0;
}

#ifdef CONFIG_PM
static int gpio_ir_recv_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_rc_dev *gpio_dev = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev))
		enable_irq_wake(gpio_to_irq(gpio_dev->gpio_nr));
	else
		disable_irq(gpio_to_irq(gpio_dev->gpio_nr));

	return 0;
}

static int gpio_ir_recv_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct gpio_rc_dev *gpio_dev = platform_get_drvdata(pdev);

	if (device_may_wakeup(dev))
		disable_irq_wake(gpio_to_irq(gpio_dev->gpio_nr));
	else
		enable_irq(gpio_to_irq(gpio_dev->gpio_nr));

	return 0;
}

static const struct dev_pm_ops gpio_ir_recv_pm_ops = {
	.suspend        = gpio_ir_recv_suspend,
	.resume         = gpio_ir_recv_resume,
};
#endif

static struct platform_driver gpio_ir_recv_driver = {
	.probe  = gpio_ir_recv_probe,
	.remove = gpio_ir_recv_remove,
	.driver = {
		.name   = GPIO_IR_DRIVER_NAME,
		.of_match_table = of_match_ptr(gpio_ir_recv_of_match),
#ifdef CONFIG_PM
		.pm	= &gpio_ir_recv_pm_ops,
#endif
	},
};
module_platform_driver(gpio_ir_recv_driver);

MODULE_DESCRIPTION("GPIO IR Receiver driver");
MODULE_LICENSE("GPL v2");
