

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/slab.h>

#include "../iio.h"
#include "../trigger.h"

static LIST_HEAD(iio_gpio_trigger_list);
static DEFINE_MUTEX(iio_gpio_trigger_list_lock);

struct iio_gpio_trigger_info {
	struct mutex in_use;
	unsigned int irq;
};

/* So what functionality do we want in here?... */
/* set high / low as interrupt type? */

static irqreturn_t iio_gpio_trigger_poll(int irq, void *private)
{
	iio_trigger_poll(private);
	return IRQ_HANDLED;
}

static DEVICE_ATTR(name, S_IRUGO, iio_trigger_read_name, NULL);

static struct attribute *iio_gpio_trigger_attrs[] = {
	&dev_attr_name.attr,
	NULL,
};

static const struct attribute_group iio_gpio_trigger_attr_group = {
	.attrs = iio_gpio_trigger_attrs,
};

static int iio_gpio_trigger_probe(struct platform_device *pdev)
{
	struct iio_gpio_trigger_info *trig_info;
	struct iio_trigger *trig, *trig2;
	unsigned long irqflags;
	struct resource *irq_res;
	int irq, ret = 0, irq_res_cnt = 0;

	do {
		irq_res = platform_get_resource(pdev,
				IORESOURCE_IRQ, irq_res_cnt);

		if (irq_res == NULL) {
			if (irq_res_cnt == 0)
				dev_err(&pdev->dev, "No GPIO IRQs specified");
			break;
		}
		irqflags = (irq_res->flags & IRQF_TRIGGER_MASK) | IRQF_SHARED;

		for (irq = irq_res->start; irq <= irq_res->end; irq++) {

			trig = iio_allocate_trigger();
			if (!trig) {
				ret = -ENOMEM;
				goto error_free_completed_registrations;
			}

			trig_info = kzalloc(sizeof(*trig_info), GFP_KERNEL);
			if (!trig_info) {
				ret = -ENOMEM;
				goto error_put_trigger;
			}
			trig->control_attrs = &iio_gpio_trigger_attr_group;
			trig->private_data = trig_info;
			trig_info->irq = irq;
			trig->owner = THIS_MODULE;
			trig->name = kmalloc(IIO_TRIGGER_NAME_LENGTH,
					GFP_KERNEL);
			if (!trig->name) {
				ret = -ENOMEM;
				goto error_free_trig_info;
			}
			snprintf((char *)trig->name,
				 IIO_TRIGGER_NAME_LENGTH,
				 "irqtrig%d", irq);

			ret = request_irq(irq, iio_gpio_trigger_poll,
					  irqflags, trig->name, trig);
			if (ret) {
				dev_err(&pdev->dev,
					"request IRQ-%d failed", irq);
				goto error_free_name;
			}

			ret = iio_trigger_register(trig);
			if (ret)
				goto error_release_irq;

			list_add_tail(&trig->alloc_list,
					&iio_gpio_trigger_list);
		}

		irq_res_cnt++;
	} while (irq_res != NULL);


	return 0;

/* First clean up the partly allocated trigger */
error_release_irq:
	free_irq(irq, trig);
error_free_name:
	kfree(trig->name);
error_free_trig_info:
	kfree(trig_info);
error_put_trigger:
	iio_put_trigger(trig);
error_free_completed_registrations:
	/* The rest should have been added to the iio_gpio_trigger_list */
	list_for_each_entry_safe(trig,
				 trig2,
				 &iio_gpio_trigger_list,
				 alloc_list) {
		trig_info = trig->private_data;
		free_irq(gpio_to_irq(trig_info->irq), trig);
		kfree(trig->name);
		kfree(trig_info);
		iio_trigger_unregister(trig);
	}

	return ret;
}

static int iio_gpio_trigger_remove(struct platform_device *pdev)
{
	struct iio_trigger *trig, *trig2;
	struct iio_gpio_trigger_info *trig_info;

	mutex_lock(&iio_gpio_trigger_list_lock);
	list_for_each_entry_safe(trig,
				 trig2,
				 &iio_gpio_trigger_list,
				 alloc_list) {
		trig_info = trig->private_data;
		iio_trigger_unregister(trig);
		free_irq(trig_info->irq, trig);
		kfree(trig->name);
		kfree(trig_info);
		iio_put_trigger(trig);
	}
	mutex_unlock(&iio_gpio_trigger_list_lock);

	return 0;
}

static struct platform_driver iio_gpio_trigger_driver = {
	.probe = iio_gpio_trigger_probe,
	.remove = iio_gpio_trigger_remove,
	.driver = {
		.name = "iio_gpio_trigger",
		.owner = THIS_MODULE,
	},
};

static int __init iio_gpio_trig_init(void)
{
	return platform_driver_register(&iio_gpio_trigger_driver);
}
module_init(iio_gpio_trig_init);

static void __exit iio_gpio_trig_exit(void)
{
	platform_driver_unregister(&iio_gpio_trigger_driver);
}
module_exit(iio_gpio_trig_exit);

MODULE_AUTHOR("Jonathan Cameron <jic23@cam.ac.uk>");
MODULE_DESCRIPTION("Example gpio trigger for the iio subsystem");
MODULE_LICENSE("GPL v2");
