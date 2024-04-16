#ifndef UART_IRQ_EEPROM_H
#define UART_IRQ_EEPROM_H

int write_bytes_to_eeprom(uint16_t address, uint8_t *data, int length);
void read_bytes_from_eeprom(uint16_t address, uint8_t *data, int length);
void write_log_entry(char* str, size_t length);

#endif //UART_IRQ_EEPROM_H
