#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "eeprom.h"
#include "lora_mod.h"

/**
 * Creates a log, which is saved in the EEPROM, sent via the LORA module and printed.
 * The current time since booting is appended to the log entry.
 * @param str String describing the content of the log message
 */
void create_log(char* str) {
    uint64_t actual_time = time_us_64() / 1000000;
    char entry[64];
    sprintf(entry, "(%llu) %s", actual_time, str);
    write_log_entry(entry, strlen(entry) + 1);
    send_lora_message(entry);
    printf("%s\n", entry);
}