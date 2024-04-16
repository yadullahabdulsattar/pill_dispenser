#ifndef UART_IRQ_STEPPER_H
#define UART_IRQ_STEPPER_H

uint8_t get_current_compartment();

bool initialize_stepper_data();

bool initialize_stepper();

void rotate_by_one_compartment();

void reset_stepper();

#endif //UART_IRQ_STEPPER_H
