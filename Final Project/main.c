#include "pico/stdlib.h"
#include "provided-libraries/uart.h"
#include "hardware/i2c.h"
#include "stepper.h"
#include "logger.h"

#define LED0_PIN 20

#define SW0_PIN 9

#define OPTO_SENSOR 28
#define PIEZO_SENSOR 27

#define UART_NR 1
#define UART_TX_PIN 4
#define UART_RX_PIN 5

#define MOTOR_CONTR_A 2
#define MOTOR_CONTR_B 3
#define MOTOR_CONTR_C 6
#define MOTOR_CONTR_D 13

#define BAUD_RATE_EEPROM 100000
#define BAUD_RATE_UART 9600

// main stages of the program
enum Stages {
    START, INITIALIZATION, REINITIALIZATION, DISPENSING
};
enum Stages program_stage;

bool led_state = false;
bool piezo_triggered = false;

void set_led(bool value);
static void gpio_handler(uint gpio, uint32_t event_mask);



int main() {

    // Initialize Button pin
    gpio_init(SW0_PIN);
    gpio_set_dir(SW0_PIN, GPIO_IN);
    gpio_pull_up(SW0_PIN);

    // Initialize interrupt handler for piezo sensor
    gpio_set_irq_enabled_with_callback(PIEZO_SENSOR, GPIO_IRQ_EDGE_FALL, true, gpio_handler);

    // Initialize LED pin
    gpio_init(LED0_PIN);
    gpio_set_dir(LED0_PIN, GPIO_OUT);

    // Initialize opto sensor pin
    gpio_init(OPTO_SENSOR);
    gpio_set_dir(OPTO_SENSOR, GPIO_IN);
    gpio_pull_up(OPTO_SENSOR);

    // Initialize piezo sensor pin
    gpio_init(PIEZO_SENSOR);
    gpio_set_dir(PIEZO_SENSOR, GPIO_IN);
    gpio_pull_up(PIEZO_SENSOR);

    // Initialize motor controller pins
    gpio_init(MOTOR_CONTR_A);
    gpio_set_dir(MOTOR_CONTR_A, GPIO_OUT);
    gpio_init(MOTOR_CONTR_B);
    gpio_set_dir(MOTOR_CONTR_B, GPIO_OUT);
    gpio_init(MOTOR_CONTR_C);
    gpio_set_dir(MOTOR_CONTR_C, GPIO_OUT);
    gpio_init(MOTOR_CONTR_D);
    gpio_set_dir(MOTOR_CONTR_D, GPIO_OUT);

    // Initialize i2c pin for eeprom
    i2c_init(i2c0, BAUD_RATE_EEPROM);
    gpio_set_function(16, GPIO_FUNC_I2C);
    gpio_set_function(17, GPIO_FUNC_I2C);

    // Initialize UART for LORA module
    uart_setup(UART_NR, UART_TX_PIN, UART_RX_PIN, BAUD_RATE_UART);

    // Initialize chosen serial port
    stdio_init_all();

    program_stage = START;

    create_log("Boot");

    // initialize stepper data and check if reinitialization necessary
    if (initialize_stepper_data()) {
        program_stage = REINITIALIZATION;
        create_log("Power off during turning in previous session");
    }

    while (true) {

        switch(program_stage) {
            case START:
                // start: blinking led until button is pressed
                set_led(!led_state);
                sleep_ms(300);
                if (!gpio_get(SW0_PIN)) {
                    while (!gpio_get(SW0_PIN)) { sleep_ms(100); }
                    set_led(0);
                    program_stage = INITIALIZATION;
                }
                break;
            case INITIALIZATION:
            case REINITIALIZATION:
                // (re-) initialization and calibration of the dispenser
                if (initialize_stepper()) {
                    if (program_stage == INITIALIZATION) {
                        // wait until button is pressed, so that dispenser can be filled up
                        set_led(1);
                        while (gpio_get(SW0_PIN)) {
                            sleep_ms(100);
                        }
                        set_led(0);
                    }
                    program_stage = DISPENSING;
                } else {
                    // if interrupt was during dispensing of the last bill, no more pills available
                    reset_stepper();
                    create_log("Dispenser empty");
                    program_stage = START;
                }
                break;
            case DISPENSING:
                // Normal operation mode: start of dispensing pills
                while (get_current_compartment() < 7) {
                    piezo_triggered = false;
                    rotate_by_one_compartment();
                    uint32_t time_snap = time_us_32();
                    if (!piezo_triggered) {
                        for (int i = 0; i < 5; i++) {
                            set_led(1);
                            sleep_ms(500);
                            set_led(0);
                            sleep_ms(500);
                        }
                        create_log("No pill dispensed");
                        uint32_t time_difference = (time_us_32() - time_snap) / 1000;
                        // subtract the time difference which was needed for sending the log via LORA
                        sleep_ms(30000 - time_difference); //-5s because of LED blinking
                    } else {
                        create_log("Pill dispensed");
                        if(get_current_compartment() < 7) {
                            uint32_t time_difference = (time_us_32() - time_snap) / 1000;
                            // subtract the time difference which was needed for sending the log via LORA
                            sleep_ms(30000 - time_difference);
                        }
                    }
                }
                reset_stepper();
                create_log("Dispenser empty");
                program_stage = START;
        }
    }
}

/**
 * changes take of the led and executes the change at the LED pin
 * @param value 0 to deactivate the led, 1 to activate it
 */
void set_led(bool value) {
    led_state = value;
    gpio_put(LED0_PIN, led_state);
}

/**
 * Interrupt handler. Triggered when the piezo sensor is triggered
 */
static void gpio_handler(uint gpio, uint32_t event_mask) {
    piezo_triggered = true;
}

