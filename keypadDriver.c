#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/input.h>
#include <linux/slab.h>

#define Num_Of_Rows 4
#define Num_Of_Cols 4


// persistent data for this keypad device
struct my_keypad
{
    struct device     *dev;

    // for setting this driver up as a "keyboard"
    struct input_dev  *input;

    struct gpio_desc *rows[Num_Of_Rows];
    struct gpio_desc *cols[Num_Of_Cols];

    unsigned int keyMap[Num_Of_Cols][Num_Of_Rows];

    unsigned int colIdToIrqMap[Num_Of_Cols];
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

    const unsigned int *map;
    unsigned int len;

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

    keypad->rows = devm_gpiod_get_array(dev, "row", GPIOD_OUT_HIGH);
    keypad->cols = devm_gpiod_get_array(dev, "col", GPIOD_IN);

    for(unsigned int i = 0; i < Num_Of_Cols; i++)
    {
        // setup interrupts for columns
        int gpioIrq = gpiod_to_irq(keypad->cols[i]);

        keypad->colIdToIrqMap[i] = gpioIrq;

        devm_request_irq(gpioIrq, my_keypad_scan, IRQF_TRIGGER_FALLING, input->name, (void *)keypad);
    }

    // setup keymap

    // get the keymap from the device tree
    map = of_get_property(dev->of_node, "linux,keymap", &len);
    if(!map)
    {
        return map;
    }

    // parse the keymap
    for(unsigned int i = 0; i < (len/sizeof(unsigned int)); i++)
    {
        unsigned int row =  (map[i] >> 24); // bits 31-24 bits are the row
        unsigned int col =  (map[i] >> 16) & 0xFF; // bits 23-16 bits are the column
        unsigned int value =  map[i] & 0xFFFF; // bits 15-0 bits are the value

        if(row >= Num_Of_Rows || col >= Num_Of_Cols)
        {
            dev_info(&pdev->dev, "Row or Col too large Col: %u Row: %u\n", row, col);
            return -1;
        }
        
        keypad->keyMap[col][row] = value;
    }

    dev_info(&pdev->dev, "Membrane keypad driver probed\n");
    return 0;
}

static irqreturn_t my_keypad_scan(int irq, void *dev_id)
{
    struct my_keypad *keypad = (struct my_keypad *)dev_id;



    return 0;
}

// do nothing but log to the device since we used the devm_ functions for allocating memory
static int my_keypad_keypad_remove(struct platform_device *pdev)
{
    struct my_keypad *keypad = platform_get_drvdata(pdev);

    dev_info(&pdev->dev, "Membrane keypad driver removed\n");
    return 0;
}

// create the table of descriptions + functions
static struct platform_driver membrane_keypad_driver = {
    .probe  = my_keypad_probe,
    .remove = my_keypad_keypad_remove,
    .driver = {
        .name = "membrane-keypad",
        .of_match_table = membrane_keypad_of_match,
    },
};

module_platform_driver(membrane_keypad_driver);

MODULE_AUTHOR("Corey Kelley");
MODULE_DESCRIPTION("Skeleton membrane keypad driver");
MODULE_LICENSE("GPL");