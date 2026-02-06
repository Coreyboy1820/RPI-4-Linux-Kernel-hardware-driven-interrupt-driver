#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>

#include <linux/workqueue.h>
#include <linux/jiffies.h>

#include <linux/input.h>

#define Num_Of_Rows 4
#define Num_Of_Cols 4


// persistent data for this keypad device
struct my_keypad
{
    struct device     *dev;

    // for setting this driver up as a "keyboard"
    struct input_dev  *input;

    struct gpio_descs *rows;
    struct gpio_descs *cols;

    unsigned int keyMap[Num_Of_Cols][Num_Of_Rows];

    unsigned int irqList[Num_Of_Cols];

    struct delayed_work work;
};

// This allows the kernel to register which DTS item this driver uses
static const struct of_device_id membrane_keypad_of_match[] = {
    { .compatible = "corey,membrane-keypad" },
    { }
};

static inline void enable_iterrupts(unsigned int *irqList, unsigned int length)
{
    for(unsigned int i = 0; i < length; i++)
    {
        enable_irq(irqList[i]);
    }
}

static inline void disable_iterrupts(unsigned int *irqList, unsigned int length)
{
    for(unsigned int i = 0; i < length; i++)
    {
        disable_irq_nosync(irqList[i]);
    }
}

static irqreturn_t my_keypad_scan_col(int irq, void *dev_id)
{
    struct my_keypad *keypad = (struct my_keypad *)dev_id;

    disable_iterrupts(keypad->irqList, Num_Of_Cols);
    schedule_delayed_work(&keypad->work, msecs_to_jiffies(5));

    return IRQ_HANDLED;
}

void get_and_register_key_press(struct work_struct *pWork)
{

    struct my_keypad *keypad =
    container_of(pWork, struct my_keypad, work.work);

    for(unsigned int rowId = 0; rowId < Num_Of_Rows; rowId++)
    {
        gpiod_set_value(keypad->rows->desc[rowId], 0);
    }

    for(unsigned int rowId = 0; rowId < Num_Of_Rows; rowId++)
    {
        gpiod_set_value(keypad->rows->desc[rowId], 1);

        for(unsigned int colId = 0; colId < Num_Of_Cols; colId++)
        {
            if(gpiod_get_value(keypad->cols->desc[colId]) > 0)
            {
                unsigned int value = keypad->keyMap[colId][rowId];
                dev_info(keypad->dev, "columnId: %X value: %X\n", colId, value);

                // register one button press each time
                input_report_key(keypad->input, value, 1);
                input_report_key(keypad->input, value, 0);
                break;
            }
        }
        
        // turn this row back off
        gpiod_set_value(keypad->rows->desc[rowId], 0);
    }

    for(unsigned int rowId = 0; rowId < Num_Of_Rows; rowId++)
    {
        gpiod_set_value(keypad->rows->desc[rowId], 1);
    }

    input_sync(keypad->input);

    enable_iterrupts(keypad->irqList, Num_Of_Cols);
}

#define request_interrupt_for_col(colIndex) \
{ \
    /*setup interrupts for columns*/ \
    int gpioIrq = gpiod_to_irq(keypad->cols->desc[colIndex]); \
    keypad->irqList[colIndex] = gpioIrq; \
    ret = devm_request_irq(keypad->dev, gpioIrq, my_keypad_scan_col, IRQF_TRIGGER_FALLING, keypad->input->name, (void *)keypad); \
    if(ret)\
    {\
        dev_info(&pdev->dev, "couldn't request irq for colindex: %u", colIndex);\
        return -1;\
    }\
} 

// make this a open firmware module
MODULE_DEVICE_TABLE(of, membrane_keypad_of_match);

// this is for setting up the keypad
static int my_keypad_probe(struct platform_device *pdev)
{
    struct my_keypad *keypad;
    struct input_dev  *input;
    int ret = 0;

    const __be32 *map;
    unsigned int len;

    keypad = devm_kzalloc(&pdev->dev, sizeof(*keypad), GFP_KERNEL);
    if (!keypad)
    {
        return -ENOMEM;
    }

    dev_info(&pdev->dev, "Allocated mem for keypad\n");

    keypad->dev = &pdev->dev;

    input = devm_input_allocate_device(&pdev->dev);
    if (!input)
    {
        return -ENOMEM;
    }

    dev_info(&pdev->dev, "Allocated input for keypad\n");

    keypad->input = input;

    /* Describe the input device */
    input->name = "Membrane Keypad";
    input->phys = "membrane/input0";

    // Chosen between, I2C, SPI, USB, and HOST, HOST is used for GPIO connections connections
    input->id.bustype = BUS_HOST;
    input->dev.parent = &pdev->dev;

    // setup keymap

    // get the keymap from the device tree
    map = of_get_property(keypad->dev->of_node, "linux,keymap", &len);
    if(!map)
    {
        return -1;
    }

    dev_info(&pdev->dev, "Got of properties\n");

    // parse the keymap
    
    for(unsigned int i = 0; i < (len/sizeof(unsigned int)); i++)
    {
        unsigned int entry = be32_to_cpu(map[i]);
        unsigned int row =  (entry >> 24); // bits 31-24 bits are the row
        unsigned int col =  (entry >> 16) & 0xFF; // bits 23-16 bits are the column
        unsigned int value =  entry & 0xFFFF; // bits 15-0 bits are the value

        dev_info(&pdev->dev, "parsed entry\n");

        if(row >= Num_Of_Rows || col >= Num_Of_Cols)
        {
            dev_info(&pdev->dev, "Row or Col too large Col: %u Row: %u\n", row, col);
            return -1;
        }
        
        keypad->keyMap[col][row] = value;
    }

    // set bit 1, indicating that this input is a key press event
    __set_bit(EV_KEY, input->evbit);

    for (int row = 0; row < Num_Of_Rows; row++) 
    {
        for (int col = 0; col < Num_Of_Cols; col++) 
        {
            unsigned int keycode = keypad->keyMap[col][row];
            dev_info(&pdev->dev, "keycode: %d\n", keycode);
            input_set_capability(input, EV_KEY, keycode);
        }
    }

    dev_info(&pdev->dev, "Set bits\n");

    // register the input device in the kernel
    ret = input_register_device(input);
    if (ret)
    {
        return ret;
    }

    dev_info(&pdev->dev, "Register Inputs\n");

    // link the driver data we created in the probe function to the kernel object
    platform_set_drvdata(pdev, keypad);

    dev_info(&pdev->dev, "Set drvdata\n");

    // init work for work queue
    INIT_DELAYED_WORK(&keypad->work, get_and_register_key_press);

    keypad->rows = devm_gpiod_get_array(keypad->dev, "row", GPIOD_OUT_HIGH);
    keypad->cols = devm_gpiod_get_array(keypad->dev, "col", GPIOD_IN);

    dev_info(&pdev->dev, "got arrays \n");

    request_interrupt_for_col(0)
    request_interrupt_for_col(1)
    request_interrupt_for_col(2)
    request_interrupt_for_col(3)

    dev_info(&pdev->dev, "Registered interrupts\n");

    for(unsigned int i = 0; i < Num_Of_Rows; i++)
    {
        gpiod_set_value(keypad->rows->desc[i], 1);

        dev_info(&pdev->dev, "set gpio output high\n");

        int rowid = desc_to_gpio(keypad->rows->desc[i]);
        int colid = desc_to_gpio(keypad->cols->desc[i]);
        dev_info(&pdev->dev, "row: %d, col: %d\n", rowid, colid);
    }

    dev_info(&pdev->dev, "Membrane keypad driver probed\n");
    return 0;
}

// do nothing but log to the device since we used the devm_ functions for allocating memory
static int my_keypad_keypad_remove(struct platform_device *pdev)
{
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