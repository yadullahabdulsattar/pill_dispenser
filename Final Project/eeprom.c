#include <stdio.h>
#include <string.h>
#include "hardware/i2c.h"

// wait time which is needed by the module after writing an entry
#define WAIT_TIME 10
// address of the eeprom on the bus
#define EEPROM_ADDR 0x50

/**
 * writes data to the eeprom at the specified address
 * @param address start address from where the data is to be saved
 * @param data pointer to the data
 * @param length Length in bytes of the data to be saved
 * @return Bytes written
 */
int write_bytes_to_eeprom(uint16_t address, uint8_t *data, int length) {
    uint8_t addr_high = (address >> 8);
    uint8_t addr_low = (address & 0xFF);
    uint8_t to_write[length + 2];
    to_write[0] = addr_high;
    to_write[1] = addr_low;
    memcpy(&to_write[2], data, sizeof(uint8_t) * length);
    int bytes_written = i2c_write_blocking(i2c0, EEPROM_ADDR, to_write, sizeof(to_write), false);
    sleep_ms(WAIT_TIME);
    return bytes_written;
}

/**
 * function to read n bytes from the EEPROM
 * @param address start address from where the data is to be read
 * @param data pointer where to save the read data
 * @param length Length in bytes of the data to be saved
 */
void read_bytes_from_eeprom(uint16_t address, uint8_t *data, int length) {
    uint8_t addr_high = (address >> 8);
    uint8_t addr_low = (address & 0xFF);
    uint8_t addr[] = {addr_high, addr_low};
    i2c_write_blocking(i2c0, EEPROM_ADDR, addr, sizeof(addr), true);
    i2c_read_blocking(i2c0, EEPROM_ADDR, data, length, false);
}

/**
 * function to calculate the crc
 * @param data_p data from which the crc should be calculated
 * @param length length of the data
 * @return the calculated crc
 */
uint16_t crc16(const uint8_t *data_p, size_t length) {
    uint8_t x;
    uint16_t crc = 0xFFFF;

    while (length--) {
        x = crc >> 8 ^ *data_p++;
        x ^= x >> 4;
        crc = (crc << 8) ^ ((uint16_t) (x << 12)) ^ ((uint16_t) (x << 5)) ^ ((uint16_t) x);
    }
    return crc;
}

/**
 * function to clear all log entries in the eeprom
 */
void clear_log() {
    uint16_t address;
    for (address = 0; address < 2048; address += 64) {
        uint8_t addr_high = (address >> 8);
        uint8_t addr_low = (address & 0xFF);
        uint8_t data[] = {addr_high, addr_low, '\0'};
        i2c_write_blocking(i2c0, EEPROM_ADDR, data, sizeof(data), false);
        sleep_ms(WAIT_TIME);
    }
}

/**
 * calculate length of an entry.
 * @param entry entry of which the length should be calculated
 * @return returns -1 if entry is longer than 61 chars otherwise the length of the entry
 */
int calculate_entry_length(const char* entry) {
    int length = 0;
    while (*entry != '\0' && length <= 62) {
        length++;
        entry++;
    }
    if (length <= 61) {
        return length;
    } else {
        return -1;
    }
}

/**
 * write entry to log in eeprom
 * @param str the string written to the log entry
 * @param length length of the string
 */
void write_log_entry(char* str, size_t length) {
    uint16_t address;
    uint16_t write_address = 0;
    char entry[64];
    bool log_full = true;
    //search for next valid write_log_entry address
    for (address = 0; address < 2048; address += 64) {
        uint8_t addr_high = (address >> 8);
        uint8_t addr_low = (address & 0xFF);
        uint8_t addr[] = {addr_high, addr_low};
        i2c_write_blocking(i2c0, EEPROM_ADDR, addr, sizeof(addr), true);
        i2c_read_blocking(i2c0, EEPROM_ADDR, (uint8_t*) entry, sizeof(entry), false);
        int entry_size = calculate_entry_length(entry);
        int crc = crc16((uint8_t*) entry, entry_size + 3);
        if (entry[0] == '\0' || entry_size == -1 || crc != 0) {
            write_address = address;
            log_full = false;
            break;
        }
    }
    //if log is full, log is cleared
    if (log_full) {
        clear_log();
        printf("Log buffer full -> Log cleared\n");
        write_address = 0;
    }
    //write entry to log at write_log_entry address with calculated crc
    uint8_t addr_high = (write_address >> 8);
    uint8_t addr_low = (write_address & 0xFF);
    uint8_t to_write[length + 4];
    to_write[0] = addr_high;
    to_write[1] = addr_low;
    memcpy(&to_write[2], str, sizeof(uint8_t) * (length + 1));
    uint16_t crc = crc16((uint8_t*) str, length);
    to_write[(length + 2)] = (uint8_t) (crc >> 8);
    to_write[(length + 3)] = (uint8_t) crc;
    i2c_write_blocking(i2c0, EEPROM_ADDR, to_write, sizeof(to_write), false);
    sleep_ms(WAIT_TIME);
}