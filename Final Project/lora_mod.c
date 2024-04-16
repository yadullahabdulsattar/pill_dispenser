#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "provided-libraries/uart.h"

#define STRLEN 80
#define TIMEOUT 5000000
#define TIMEOUT_MSG 10000000

#define LORA_UART 1

// stages for sending a message via the LORA module
enum Stages {
    ESTABLISH_CONNECTION, SET_MODE, SET_APP_KEY, SET_CLASS, SET_PORT, JOIN, SEND_MESSAGE, FINISHED, ERROR
};
enum Stages;

/**
 * // function to execute a command on the LORA module and return the answer
 * @param command command to execute
 * @param output pointer where the return should be written to
 * @return returns true if successful, false when a timeout occurred
 */
bool execute_command(const char *command, char *output) {
    char str[STRLEN];
    int count = 0;
    int pos = 0;
    strcpy(output, "");

    while (count < 5) {
        uart_send(LORA_UART, command);
        uint32_t t = time_us_32();
        // if no linefeed is in the output yet or the timeout is reached,
        // add any output in the buffer to the output string
        while (strchr(output, '\n') == NULL && (time_us_32() - t) <= TIMEOUT) {
            pos = uart_read(LORA_UART, (uint8_t *) str, STRLEN);
            if (pos > 0) {
                str[pos] = '\0';
                strcat(output, str);
                t = time_us_32();
            }
        }
        if (strchr(output, '\n') != NULL) {
            // newline detected, the command was successful
            return true;
        } else {
            // no newline means the timeout occured
            count++;
        }
    }
    return false;
}

bool send_message(const char *command, char *output) {
    char str[STRLEN];
    int count = 0;
    int pos = 0;
    strcpy(output, "");
    char done[] = "MSG: Done";

    while (count < 5) {
        uart_send(LORA_UART, command);
        uint32_t t = time_us_32();
        // if no linefeed is in the output yet or the timeout is reached,
        // add any output in the buffer to the output string
        while (strstr(output, done) == NULL && (time_us_32() - t) <= TIMEOUT_MSG) {
            pos = uart_read(LORA_UART, (uint8_t *) str, STRLEN);
            if (pos > 0) {
                str[pos] = '\0';
                strcat(output, str);
                t = time_us_32();
            }
        }
        if (strstr(output, done) != NULL) {
            // newline detected, the command was successful
            return true;
        } else {
            // no newline means the timeout occured
            count++;
        }
    }
    return false;
}

/**
 * function to send a message via the LORA module
 * @param str message to send
 * @return true if message was sent successfully, otherwise false
 */
bool send_lora_message(char* str) {
    enum Stages stage = ESTABLISH_CONNECTION;

    const char atCommand[] = "at\r\n";
    const char setModeCommand[] = "at+MODE=LWOTAA\r\n";
    const char setAppKeyCommand[] = "at+KEY=APPKEY, \"b8c1c1466ded5d2fb668555b36d152f7\"\r\n";
    const char setClassCommand[] = "at+CLASS=A\r\n";
    const char setPortCommand[] = "at+PORT=8\r\n";
    const char joinCommand[] = "at+JOIN\r\n";
    char messageCommand[150];
    char output[300] = "";

    while (true) {
        switch (stage) {
            case ESTABLISH_CONNECTION:
                // Establish connection and execute test command at
                if (execute_command(atCommand, output)) {
                    printf("Connected to LoRa module\r\n");
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module not responding\r\n");
                    stage = ERROR;
                }
                break;
            case SET_MODE:
                // Set work mode
                if (execute_command(setModeCommand, output)) {
                    printf("%s", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: set mode\r\n");
                    stage = ERROR;
                }
                break;
            case SET_APP_KEY:
                // Set LoRaWAN related AES-128 KEY
                if (execute_command(setAppKeyCommand, output)) {
                    printf("%s", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: set app key\r\n");
                    stage = ERROR;
                }
                break;
            case SET_CLASS:
                // Set class mode
                if (execute_command(setClassCommand, output)) {
                    printf("%s", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: set class\r\n");
                    stage = ERROR;
                }
                break;
            case SET_PORT:
                // Set port
                if (execute_command(setPortCommand, output)) {
                    printf("%s", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: set port\r\n");
                    stage = ERROR;
                }
                break;
            case JOIN:
                // Try a join
                if (execute_command(joinCommand, output)) {
                    printf("%s", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: join\r\n");
                    stage = ERROR;
                }
                break;
            case SEND_MESSAGE:
                // Send the message via the LORA module
                sprintf(messageCommand, "at+MSG=\"%s\"\r\n", str);
                if (send_message(messageCommand, output)) {
                    printf("%s\n", output); // print out output
                    stage++;
                } else {
                    // if execute_command returned false, the timeout occurred
                    printf("Module stopped responding: send message\r\n");
                    stage = ERROR;
                }
                break;
            case FINISHED:
                printf("Message sent successfully\n");
                return true;
            case ERROR:
                return false;
        }
    }
}