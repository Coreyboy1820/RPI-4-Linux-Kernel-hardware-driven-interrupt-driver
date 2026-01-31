#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/slab.h>


// persistent data for this keypad device
struct my_keypad
{
    struct device     *dev;

    // for setting this driver up as a "keyboard"
    struct input_dev  *input;
};

// This allows the kernel to register which DTS item this driver uses
static const struct of_device_id membrane_keypad_of_match[] = {
    { .compatible = "corey,membrane-keypad" },
    { }
};

// make this a open firmware module
MODULE_DEVICE_TABLE(of, membrane_keypad_of_match);

// this is for setting up the keypad
static int my_keypad_probe(struct platform_device *pdev)
{
    struct my_keypad *keypad;
    struct input_dev  *input;
    int ret;

    keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
    if (!keypad)
    {
        return -ENOMEM;
    }

    keypad->dev = &pdev->dev;

    input = devm_input_allocate_device(&pdev->dev);
    if (!input)
    {
        return -ENOMEM;
    }

    keypad->input = input;

    /* Describe the input device */
    input->name = "Membrane Keypad";
    input->phys = "membrane/input0";

    // Chosen between, I2C, SPI, USB, and HOST, HOST is used for GPIO connections connections
    input->id.bustype = BUS_HOST;
    input->dev.parent = &pdev->dev;

    // set bit 1, indicating that this input is a key press event
    __set_bit(EV_KEY, input->evbit);

    // register the input device in the kernel
    ret = input_register_device(input);
    if (ret)
    {
        return ret;
    }

    // link the driver data we created in the probe function to the kernel object
    platform_set_drvdata(pdev, keypad);

    dev_info(&pdev->dev, "Membrane keypad driver probed\n");
    return 0;
}

// do nothing but log to the device since we used the devm_ functions for allocating memory
static int membrane_keypad_remove(struct platform_device *pdev)
{
    struct membrane_keypad *keypad = platform_get_drvdata(pdev);

    dev_info(&pdev->dev, "Membrane keypad driver removed\n");
    return 0;
}

// create the table of descriptions + functions
static struct platform_driver membrane_keypad_driver = {
    .probe  = membrane_keypad_probe,
    .remove = membrane_keypad_remove,
    .driver = {
        .name = "membrane-keypad",
        .of_match_table = membrane_keypad_of_match,
    },
};

module_platform_driver(membrane_keypad_driver);

MODULE_AUTHOR("Corey Kelley");
MODULE_DESCRIPTION("Skeleton membrane keypad driver");
MODULE_LICENSE("GPL");