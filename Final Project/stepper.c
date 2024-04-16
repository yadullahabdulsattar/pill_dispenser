#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "stepper.h"
#include "eeprom.h"
#include "logger.h"

#define OPTO_SENSOR 28

#define MOTOR_CONTR_A 2
#define MOTOR_CONTR_B 3
#define MOTOR_CONTR_C 6
#define MOTOR_CONTR_D 13

// stages for calibrate the stepper motor after the start
enum Calibration_Stages {
    WAY_TO_START, CALCULATE_SENSOR_WIDTH, CALCULATE_NON_SENSOR_WIDTH, WAY_TO_ZERO, CALIBRATION_FINISHED
};
enum Calibration_Stages calibration_stage;

// stages which describe the current state of the stepper
enum Stepper_Stages {
    INITIAL = 0, // initial state of the motor before successful calibration
    RECALIBRATING = 1, // stepper motor is currently recalibrating
    NORMAL_OPERATION = 2, // normal, calibrated operation mode
    TURNING = 3 // stepper motor is currently turning by one compartment
};
enum Stepper_Stages stepper_stage;

// structure to describe the current state of stepper motor
typedef struct stepperstate {
    uint8_t stepper_stage; // describes the stage as defined in enum stepper stages
    uint8_t current_compartment; // describes the actual compartment position in relation to the point zero
} stepperstate;

// structure with the calibrated data of the stepper motor
typedef struct stepperdata {
    uint16_t revolution_steps; // steps needed for one total revolution
    uint16_t sensor_width; // amount of steps within the opto sensor detection
} stepperdata;

stepperdata stepper_data = {4096, 0};
stepperstate stepper_state = {0, 0};

// current step in driver sequence
uint8_t current_step = 0;

// eeprom address where the stepper state structure is saved
uint16_t eeprom_address_stepperstate = 0x7FFE;
// eeprom address where the stepper data structure is saved
uint16_t eeprom_address_stepperdata = 0x7FFA;

// driver sequence to set the motor controllers for a rotation of the motor
int seq_table[8][4] = {
        {1, 0, 0, 0},
        {1, 1, 0, 0},
        {0, 1, 0, 0},
        {0, 1, 1, 0},
        {0, 0, 1, 0},
        {0, 0, 1, 1},
        {0, 0, 0, 1},
        {1, 0, 0, 1}
};

void set_motor_controllers(int sleep_timer);
bool recalibration();
void calibration();
void load_stepper_data();
void save_stepper_data();
void load_stepper_state();
void save_stepper_state();
void run(int compartments);

/**
 * method to load the stepper data structure from the EEPROM
 */
void load_stepper_data() {
    uint8_t divided_data[4];
    read_bytes_from_eeprom(eeprom_address_stepperdata, divided_data, sizeof(divided_data));
    stepper_data.revolution_steps = (divided_data[0] << 8) | divided_data[1];
    stepper_data.sensor_width = (divided_data[2] << 8) | divided_data[3];
}

/**
 * method to save the stepper data structure to the EEPROM
 */
void save_stepper_data() {
    uint8_t divided_data[4];
    divided_data[0] = (stepper_data.revolution_steps >> 8);
    divided_data[1] = (stepper_data.revolution_steps & 0xFF);
    divided_data[2] = (stepper_data.sensor_width >> 8);
    divided_data[3] = (stepper_data.sensor_width & 0xFF);
    write_bytes_to_eeprom(eeprom_address_stepperdata, divided_data, sizeof(divided_data));
}

/**
 * method to load the stepper state structure from the EEPROM
 */
void load_stepper_state() {
    read_bytes_from_eeprom(eeprom_address_stepperstate, (uint8_t *) &stepper_state, sizeof(stepper_state));
}

/**
 * method to save the stepper state structure to the EEPROM
 */
void save_stepper_state() {
    write_bytes_to_eeprom(eeprom_address_stepperstate, (uint8_t *) &stepper_state, sizeof(stepper_state));
}

/**
 * returns the current compartment position of the stepper motor in relation to "point zero"
 * @return the current compartment
 */
uint8_t get_current_compartment() {
    return stepper_state.current_compartment;
}

/**
 * initializes the stepper data after booting by loading the data from the EEPROM
 * @return true if the current stepper state is not INITIAL, otherwise false
 */
bool initialize_stepper_data() {
    load_stepper_data();
    load_stepper_state();

    return (stepper_state.stepper_stage != INITIAL);
}

/**
 * initializes and (re-) calibrates the stepper based on possible interruptions during the last operation
 * @return true if the dispenser has not become empty after recalibration and has not reached "point zero" again, otherwise false
 */
bool initialize_stepper() {
    bool dispenser_not_empty = true;

    if (stepper_state.stepper_stage == RECALIBRATING || stepper_state.stepper_stage == TURNING) {
        dispenser_not_empty = recalibration();
        sleep_ms(2000);
    } else if (stepper_state.stepper_stage == NORMAL_OPERATION) {
        // stepper was interrupted during normal operation, now continues at the stopped point
        sleep_ms(500);
    } else {
        // default case: corrupted data or start of program leads to the calibration of the motor
        calibration();
    }

    return dispenser_not_empty;
}

/**
 * method to recalibrate the stepper motor after power off during middle of a turn
 * @return true if stepper motor did not reach point zero while recalibrating otherwise false
 */
bool recalibration() {
    stepper_state.stepper_stage = RECALIBRATING;
    save_stepper_state();
    create_log("Recalibration started");

    bool edge_detected = false;
    int last_sensor_value = gpio_get(OPTO_SENSOR);
    // motor is turned backwards until the opto sensor is triggered
    while (!edge_detected) {
        set_motor_controllers(2);
        if (gpio_get(OPTO_SENSOR) == 0 && last_sensor_value == 1) {
            edge_detected = true;
        }
        last_sensor_value = gpio_get(OPTO_SENSOR);
        if (current_step == 0) {
            current_step = 7;
        } else {
            current_step--;
        }
    }
    // motor continues moving backward until middle of the opto sensor (point zero) is reached
    for (int i = 0; i < (stepper_data.sensor_width / 2); i++) {
        set_motor_controllers(3);
        if (current_step == 0) {
            current_step = 7;
        } else {
            current_step--;
        }
    }
    uint8_t new_compartment = stepper_state.current_compartment + 1;
    // stepper motor moves forward until next intact compartment
    run(stepper_state.current_compartment + 1);
    // check if point zero was reached again and all pills have been dispensed
    if (new_compartment <= 7) {
        stepper_state.current_compartment = new_compartment;
        stepper_state.stepper_stage = NORMAL_OPERATION;
        save_stepper_state();
        return true;
    } else {
        // all pills have been dispensed
        stepper_state.current_compartment = 0;
        stepper_state.stepper_stage = INITIAL;
        save_stepper_state();
        return false;
    }
}

/**
 * function for calibration after program started with no interrupts detected
 */
void calibration() {
    create_log("Calibration started");
    int step_counter;
    stepper_data.sensor_width = 0;
    int last_sensor_value = gpio_get(OPTO_SENSOR);
    bool edge_detected = false;
    calibration_stage = WAY_TO_START;
    int sensor_width_values[2] = {0, 0};
    int revolution_steps_values[2] = {0, 0};
    int round = 0;
    while (calibration_stage != CALIBRATION_FINISHED) {
        switch (calibration_stage) {
            case WAY_TO_START:
                // move forward until beginning of the opto sensor detection
                edge_detected = false;
                while (!edge_detected) {
                    set_motor_controllers(2);
                    if (gpio_get(OPTO_SENSOR) == 0 && last_sensor_value == 1) {
                        edge_detected = true;
                    }
                    last_sensor_value = gpio_get(OPTO_SENSOR);
                    if (current_step == 7) {
                        current_step = 0;
                    } else {
                        current_step++;
                    }
                }
                calibration_stage = CALCULATE_SENSOR_WIDTH;
                break;
            case CALCULATE_SENSOR_WIDTH:
                // calculate the steps during opto sensor detection
                edge_detected = false;
                while (!edge_detected) {
                    set_motor_controllers(3);
                    if (gpio_get(OPTO_SENSOR) == 1 && last_sensor_value == 0) {
                        edge_detected = true;
                    }
                    last_sensor_value = gpio_get(OPTO_SENSOR);
                    sensor_width_values[round]++;
                    if (current_step == 7) {
                        current_step = 0;
                    } else {
                        current_step++;
                    }
                }
                calibration_stage = CALCULATE_NON_SENSOR_WIDTH;
                break;
            case CALCULATE_NON_SENSOR_WIDTH:
                // calculate all the steps outside the opto sensor detection
                step_counter = 0;
                edge_detected = false;
                while (!edge_detected) {
                    set_motor_controllers(3);
                    if (gpio_get(OPTO_SENSOR) == 0 && last_sensor_value == 1) {
                        edge_detected = true;
                    }
                    last_sensor_value = gpio_get(OPTO_SENSOR);
                    step_counter++;
                    if (current_step == 7) {
                        current_step = 0;
                    } else {
                        current_step++;
                    }
                }
                revolution_steps_values[round] = step_counter + sensor_width_values[round];;
                if (round < 1) {
                    // second calibration round for better accuracy
                    round++;
                    calibration_stage = CALCULATE_SENSOR_WIDTH;
                } else {
                    calibration_stage = WAY_TO_ZERO;
                }
                break;
            case WAY_TO_ZERO:
                // use average of both measurement rounds as calibration data
                stepper_data.revolution_steps = (revolution_steps_values[0] + revolution_steps_values[1]) / 2;
                stepper_data.sensor_width = (sensor_width_values[0] + sensor_width_values[1]) / 2;
                printf("Calculated revolution steps: %d\n", stepper_data.revolution_steps);
                // move forward until "point zero" where to start the normal operation
                for (int i = 0; i < (stepper_data.sensor_width / 2); i++) {
                    set_motor_controllers(3);
                    if (current_step == 7) {
                        current_step = 0;
                    } else {
                        current_step++;
                    }
                }
                calibration_stage = CALIBRATION_FINISHED;
                save_stepper_data();
                stepper_state.stepper_stage = NORMAL_OPERATION;
                stepper_state.current_compartment = 0;
                save_stepper_state();
                break;
        }
    }
}

/**
 * method to set all controllers of the motor with the specified values from the current step of the driver sequence
 * @param sleep_timer time in ms to wait after this, controls the speed
 */
void set_motor_controllers(int sleep_timer) {
    gpio_put(MOTOR_CONTR_A, seq_table[current_step][0]);
    gpio_put(MOTOR_CONTR_B, seq_table[current_step][1]);
    gpio_put(MOTOR_CONTR_C, seq_table[current_step][2]);
    gpio_put(MOTOR_CONTR_D, seq_table[current_step][3]);
    sleep_ms(sleep_timer);
}

/**
 * move the stepper motor by n compartments forward
 * @param compartments amount of compartments to move forward
 */
void run(int compartments) {
    int calculated_steps = (int) (compartments * ((double) stepper_data.revolution_steps / 8));
    for (int i = 0; i < calculated_steps; i++) {
        set_motor_controllers(2);
        if (current_step == 7) {
            current_step = 0;
        } else {
            current_step++;
        }
    }
}

/**
 * to rotate the stepper motor by one compartment,
 * whereby the state structure of the stepper motor is changed according to this action
 */
void rotate_by_one_compartment() {
    stepper_state.stepper_stage = TURNING;
    save_stepper_state();
    run(1);
    stepper_state.current_compartment++;
    stepper_state.stepper_stage = NORMAL_OPERATION;
    save_stepper_state();
}

/**
 * reset the state of the motor to the initial state with no calibration data
 */
void reset_stepper() {
    stepper_state.current_compartment = 0;
    stepper_state.stepper_stage = INITIAL;
    save_stepper_state();
}