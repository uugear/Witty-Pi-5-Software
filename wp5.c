#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <ctype.h>

#include "wp5lib.h"

#define INPUT_MAX_LENGTH        32

#define DOWNLOAD_BUFFER_SIZE    1024

#define IN_USE_SCRIPT_NAME      "schedule"


bool running = true;

int model = MODEL_UNKNOWN;


/**
 * Signal handler
 */
void handle_signal(int signum) {
    
    running = false;
    
    printf("\nExit now.\n");
    
    exit(0);
}


/**
 * Accept a charactor from user input
 *
 * @return the charactor from user input
 */
char input_charactor(void) {
	char input_char;
    int c;
    input_char = getchar();
    while ((c = getchar()) != '\n' && c != EOF);
	return input_char;
}


/**
 * Accept a Y/y or N/n letter from user input
 *
 * @param msg The prompt to show to user
 * @param indent The indent for printf
 * @return true if user confirm, false otherwise
 */
bool user_confirm(char * msg, int indent) {
	for (int i = 0; i < indent; i ++) printf(" ");
    printf("%s\n", msg);
	for (int i = 0; i < indent; i ++) printf(" ");
    printf("Please confirm (Y/N): ");
	switch (input_charactor()) {
		case 'y':
		case 'Y':
		return true;
	}
	return false;
}


/**
 * Accept a number from user input
 *
 * @param min The minimum number to accept
 * @param max The maximum number to accept
 * @param value The pointer to integer variable for value
 * @param valid The pointer to bool variable for validity
 * @param indent The indent for printf
 * @return false if user input is empty, otherwise true
 */
bool input_number(int min, int max, int *value, bool *valid, int indent) {
	char input[INPUT_MAX_LENGTH];
    if (fgets(input, sizeof(input), stdin) != NULL) {
        char *newline = strchr(input, '\n');
        if (newline != NULL) {
            *newline = '\0';
        } else {
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
        }
    }
	if (input[0] == '\0') {
		return false;	// Empty input
	}
    char *end;
    if (valid) *valid = true;
    if (value) *value = strtol(input, &end, 10);
    if (*end != '\0') {
        if (valid) *valid = false;
		for (int i = 0; i < indent; i ++) printf(" ");
        printf("Please input a number.\n");
    } else if (!value || *value < min || *value > max) {
        if (valid) *valid = false;
		for (int i = 0; i < indent; i ++) printf(" ");
        printf("Please enter a number between %d and %d.\n", min, max);
    }
	return true;
}


/**
 * Show user a message and request to input a number
 *
 * @param msg The message to show to user
 * @param min The minimum number to accept
 * @param max The maximum number to accept
 * @param value The pointer to variable for value
 * @param indent The indent for printf
 * @return true if user input is valid, otherwise false
 */
bool request_input_number(char * msg, int min, int max, int *value, int indent) {
	for (int i = 0; i < indent; i ++) printf(" ");
	printf(msg);
	bool valid;
	bool has_data = input_number(min, max, value, &valid, indent);
	if (!has_data) {
		printf("\n");
	}
	return has_data && valid;
}


/**
 * Read a line from stdin with buffer overflow protection
 * 
 * @param buffer The buffer to store the input
 * @param size The size of the buffer
 * @return The buffer if successful, NULL otherwise
 */
static char *read_line(char *buffer, size_t size) {
    if (fgets(buffer, size, stdin) == NULL) {
        return NULL;
    }
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
    return buffer;
}


/**
 * Display the information bar
 */
void do_info_bar(void) {
    int model = get_wittypi_model();
    if (model == MODEL_UNKNOWN) {
        printf("Can not detect Witty Pi, exiting...\n");
		exit(0);
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("  Model: %s", wittypi_models[model]);
    float celsius = get_temperature();
    float fahrenheit = celsius_to_fahrenheit(celsius);
	printf("   Temperature: %.3f°C / %.3f°F\n", celsius, fahrenheit);
    int mode = get_power_mode();
    if (mode == 0) {
        printf("  V-USB: %.3fV", get_vusb());
    } else if (mode == 1) {
        printf("  V-IN: %.3fV", get_vin());
    }
    printf("   V-OUT: %.3fV", get_vout());
    printf("   I-OUT: %.3fA\n", get_iout());
	
	DateTime sys_dt, rtc_dt;
	if (get_system_time(&sys_dt)) {
		printf("  SYS Time: %4d-%02d-%02d %02d:%02d:%02d\n", sys_dt.year, sys_dt.month, sys_dt.day, sys_dt.hour, sys_dt.min, sys_dt.sec);
	}
	if (get_rtc_time(&rtc_dt)) {
		printf("  RTC Time: %4d-%02d-%02d %02d:%02d:%02d\n", rtc_dt.year, rtc_dt.month, rtc_dt.day, rtc_dt.hour, rtc_dt.min, rtc_dt.sec);
	}
    printf("--------------------------------------------------------------------------------\n");
}


/**
 * Check if a string contains a valid integer within a specified range
 * 
 * @param str The string to check
 * @param min Minimum allowed value
 * @param max Maximum allowed value
 * @param result Pointer to store the parsed integer if valid
 * @return true if valid, false otherwise
 */
static bool is_valid_integer_in_range(const char *str, int min, int max, int *result) {
    char *endptr;
    if (str == NULL || *str == '\0') {
        return false;
    }
    for (int i = 0; str[i] != '\0'; i++) {
        if (i == 0 && str[i] == '-') {
            continue;  // Allow leading minus sign
        }
        if (!isdigit((unsigned char)str[i])) {
            return false;
        }
    }
    long val = strtol(str, &endptr, 10);
    if (*endptr != '\0') {
        return false;
    }
    if (val < min || val > max) {
        return false;
    }
    *result = (int)val;
    return true;
}


/**
 * Checks if a string matches the format "dd HH:MM:SS"
 * 
 * @param input The string to check
 * @return true if it matches, false otherwise
 */
static bool validate_time_format(const char *input) {
    regex_t regex;
    int ret = regcomp(&regex, "^[0-3][0-9][[:space:]][0-2][0-9]:[0-5][0-9]:[0-5][0-9]$", REG_EXTENDED);
    if (ret) {
        fprintf(stderr, "Could not compile regex\n");
        return false;
    }
    ret = regexec(&regex, input, 0, NULL, 0);
    regfree(&regex);
    return ret == 0;
}


/**
 * Parse time string in format "dd HH:MM:SS" into individual components
 * 
 * @param when The time string
 * @param date Pointer to store the date
 * @param hour Pointer to store the hour
 * @param minute Pointer to store the minute
 * @param second Pointer to store the second
 * @return true if parsed successfully, false otherwise
 */
static bool parse_time_string(const char *when, uint8_t *date, uint8_t *hour, uint8_t *minute, uint8_t *second) {
    int d, h, m, s;
    if (sscanf(when, "%d %d:%d:%d", &d, &h, &m, &s) != 4) {
        return false;
    }
    *date = (uint8_t)d;
    *hour = (uint8_t)h;
    *minute = (uint8_t)m;
    *second = (uint8_t)s;
    return true;
}


/**
 * Schedule a startup time for the Raspberry Pi
 */
void schedule_startup(void) {
    char input[INPUT_MAX_LENGTH];
    uint8_t date, hour, minute, second;
    
    if (get_startup_time(&date, &hour, &minute, &second)) {
        printf("  Auto startup time is currently set to \"%02d %02d:%02d:%02d\"\n", date, hour, minute, second);
    } else {
        printf("  Auto startup time is not set yet.\n");
    }
    
    printf("  When do you want your Raspberry Pi to auto startup? (dd HH:MM:SS) ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("  Error reading input.\n");
        return;
    }
    
    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n') {
        input[len-1] = '\0';
    }
    
    if (!validate_time_format(input)) {
        printf("  Invalid input detected :-(\n");
        return;
    }
    
    if (!parse_time_string(input, &date, &hour, &minute, &second)) {
        printf("  Failed to parse input.\n");
        return;
    }
    
    if (date > 31 || date < 1) {
        printf("  Day value should be 01~31.\n");
        return;
    } else if (hour > 23) {
        printf("  Hour value should be 00~23.\n");
        return;
    }
    
	// TODO: check if RTC and system time is synchronized

	printf("  Setting startup time to \"%s\"\n", input);
	set_startup_time(date, hour, minute, second);
	printf("  Done :-)\n");
    
}


/**
 * Schedule a shutdown time for the Raspberry Pi
 */
void schedule_shutdown(void) {
    char input[INPUT_MAX_LENGTH];
    uint8_t date, hour, minute, second;
    
    if (get_shutdown_time(&date, &hour, &minute, &second)) {
        printf("  Auto shutdown time is currently set to \"%02d %02d:%02d:%02d\"\n", date, hour, minute, second);
    } else {
        printf("  Auto shutdown time is not set yet.\n");
    }
    
    printf("  When do you want your Raspberry Pi to auto shutdown? (dd HH:MM:SS) ");
    if (fgets(input, sizeof(input), stdin) == NULL) {
        printf("  Error reading input.\n");
        return;
    }
    
    size_t len = strlen(input);
    if (len > 0 && input[len-1] == '\n') {
        input[len-1] = '\0';
    }
    
    if (!validate_time_format(input)) {
        printf("  Invalid input detected :-(\n");
        return;
    }
    
    if (!parse_time_string(input, &date, &hour, &minute, &second)) {
        printf("  Failed to parse input.\n");
        return;
    }
    
    if (date > 31 || date < 1) {
        printf("  Day value should be 01~31.\n");
        return;
    } else if (hour > 23) {
        printf("  Hour value should be 00~23.\n");
        return;
    }
	
	// TODO: check if RTC and system time is synchronized
    
	printf("  Setting shutdown time to \"%s\"\n", input);
	set_shutdown_time(date, hour, minute, second);
	printf("  Done :-)\n");
}


// Print file list, or copy selected file name to output buffer
int list_or_select_file(const char* input, int select, char* output, int output_len) {
    const char* start = input + 1;
    const char* current = start;
    int file_count = 0;
    
    while (*current && *current != '>') {
        const char* filename_start = current;
        
        while (*current && *current != '|' && *current != '>') {
            current++;
        }
        
        if (*current != '>' && strncmp(filename_start, IN_USE_SCRIPT_NAME, strlen(IN_USE_SCRIPT_NAME)) != 0) {
            file_count ++;
            int len = current - filename_start;
            if (select <= 0) {
                printf("  [%d] %.*s\n", file_count, len, filename_start);
            } else if (select == file_count && output && output_len) {
                snprintf(output, output_len, "%.*s", len, filename_start);
                return select;
            }
        }
        
        if (*current == '|') {
            current++;
        }
    }
    return file_count;
}


/**
 * Packs a filename into the specified packet format with CRC8 checksum
 *
 * @param filename Input filename string
 * @param output Output buffer to store the packed packet
 * @return bool true if packing successful, false otherwise
 */
bool pack_filename(char* filename, char* output) {
    if (filename == NULL || output == NULL) {
        return false;
    }
    if (strlen(filename) == 0) {
        return false;
    }
    size_t filename_len = strlen(filename);
    memmove(output + 1, filename, filename_len + 1);
    output[0] = PACKET_BEGIN;
    uint8_t crc8 = calculate_crc8((const uint8_t*)output, filename_len + 1);
    output[filename_len + 1] = PACKET_DELIMITER;
    output[filename_len + 2] = crc8;
    output[filename_len + 3] = PACKET_END;
    output[filename_len + 4] = '\0';
    return true;
}


/**
 * Choose a schedule script
 */
void choose_schedule_script(void) {
    i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE);
    run_admin_command(I2C_ADMIN_PWD_CMD_LIST_FILES);
    
    char buf[DOWNLOAD_BUFFER_SIZE];
    int len = i2c_read_stream_util(-1, I2C_ADMIN_DOWNLOAD, buf, DOWNLOAD_BUFFER_SIZE - 1, '>');
    buf[len] = '\0';
    
    printf("  Available schedule scripts on disk:\n");
    int file_count = list_or_select_file(buf, -1, NULL, -1);
    int select = 0;
    if (request_input_number("Please choose a schedule script: ", 1, file_count, &select, 2)) {
        list_or_select_file(buf, select, buf, DOWNLOAD_BUFFER_SIZE);
        printf("  You have chosen %s\n", buf);
        printf("  Please wait while processing...");
        fflush(stdout);

        pack_filename(buf, buf);
        i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE);
        i2c_write_stream_util(-1, I2C_ADMIN_UPLOAD, buf, DOWNLOAD_BUFFER_SIZE, '>');
        run_admin_command(I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT);
        
        sleep(1);
        int model = MODEL_UNKNOWN;
        while (model == MODEL_UNKNOWN) {
            sleep(1);
            model = get_wittypi_model();
        }
        printf("done :)\n");
    }
}


/**
 * Configure the low voltage threshold
 */
void configure_low_voltage_threshold(void) {
    char input[INPUT_MAX_LENGTH];
    float threshold = get_low_voltage_threshold();
	if (threshold >= 0) {
		printf("  Low voltage threshold is currently set to %.1fV\n", threshold);
	}
    
    if (model == MODEL_WITTYPI_5_L3V7) {	// Witty Pi 5 L3V7
        printf("  Input new low voltage (3.0~4.2, value in volts, 0=Disabled): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("  Error reading input.\n");
            return;
        }
        threshold = atof(input);
        if (threshold >= 3.0 && threshold <= 4.2) {
            if (set_low_voltage_threshold(threshold)) {
				printf("  Low voltage threshold is set to %.1fV\n", threshold);
                sleep(2);
            } else {
                printf("  Failed to set low voltage threshold.\n");
            }
        } else if (threshold == 0.0) {
            if (set_low_voltage_threshold(threshold)) {
                printf("  Disabled low voltage threshold!\n");
                sleep(2);
            } else {
                printf("  Failed to disable low voltage threshold.\n");
            }
        } else {
            printf("  Please input from 3.0 to 4.2 ...\n");
            sleep(2);
        }
    } else {	// Other Witty Pi models
        printf("  Input new low voltage (2.0~25.0, value in volts, 0=Disabled): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("  Error reading input.\n");
            return;
        }
        threshold = atof(input);
        if (threshold >= 2.0 && threshold <= 25.0) {
            if (set_low_voltage_threshold(threshold)) {
                printf("  Low voltage threshold is set to %.1fV\n", threshold);
                sleep(2);
            } else {
                printf("  Failed to set low voltage threshold.\n");
            }
        } else if (threshold < 0.01f && threshold > -0.01f) {
            if (set_low_voltage_threshold(0.0f)) {
                printf("  Disabled low voltage threshold!\n");
                sleep(2);
            } else {
                printf("  Failed to disable low voltage threshold.\n");
            }
        } else {
            printf("Please input from 2.0 to 25.0 ...\n");
            sleep(2);
        }
    }
}


/**
 * Configure the recovery voltage threshold
 */
void configure_recovery_voltage_threshold(void) {
	char input[INPUT_MAX_LENGTH];
    float threshold = get_recovery_voltage_threshold();
	if (threshold >= 0) {
		printf("  Recovery voltage threshold is currently set to %.1fV\n", threshold);
	}
    
    if (model == MODEL_WITTYPI_5_L3V7) {	// Witty Pi 5 L3V7
        printf("  Turn on RPi when USB 5V is connected (0=No, 1=Yes): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("  Error reading input.\n");
            return;
        }
        int action = atoi(input);
        if (action == 0) {
            if (set_recovery_voltage_threshold(0.0f)) {
                printf("  Will do nothing when USB 5V is connected.\n");
                sleep(2);
            } else {
                printf("  Failed to set recovery voltage action.\n");
            }
        } else if (action == 1) {
            if (set_recovery_voltage_threshold(0.1f)) {
                printf("  Will turn on RPi when USB 5V is connected.\n");
                sleep(2);
            } else {
                printf("  Failed to set recovery voltage action.\n");
            }
        } else {
            printf("  Please input 0 or 1\n");
        }
    } else {	// Other Witty Pi models
        printf("  Input new recovery voltage (2.0~25.0, value in volts, 0=Disabled): ");
        if (fgets(input, sizeof(input), stdin) == NULL) {
            printf("  Error reading input.\n");
            return;
        }
        threshold = atof(input);
        if (threshold >= 2.0 && threshold <= 25.0) {
            if (set_recovery_voltage_threshold(threshold)) {
				printf("  Recovery voltage threshold is set to %.1fV\n", threshold);
                sleep(2);
            } else {
                printf("  Failed to set recovery voltage threshold.\n");
            }
        } else if (threshold < 0.01f && threshold > -0.01f) {
            if (set_recovery_voltage_threshold(0.0f)) {
                printf("  Disabled recovery voltage threshold!\n");
                sleep(2);
            } else {
                printf("  Failed to disable recovery voltage threshold.\n");
            }
        } else {
            printf("  Please input from 2.0 to 25.0 ...\n");
            sleep(2);
        }
    }
}


/**
 * Format temperature action message
 * 
 * @param below true for below temperature action, false for over temperature action
 * @param action Action value (1=Shutdown, 2=Startup), use -1 to get current value
 * @param temperature Temperature threshold
 * @param buffer The pointer to buffer
 * @param buf_size The size of buffer
 * @return Length of the string written, negative if failed
 */
int temperature_action_info(bool below, int action, int temperature, char * buffer, int buf_size) {
	if (!buffer || buf_size <= 0) {
		return -1;
	}
    if (action == -1) {
        action = (int)i2c_get(-1, below ? I2C_CONF_BELOW_TEMP_ACTION : I2C_CONF_OVER_TEMP_ACTION);
        temperature = (int8_t)i2c_get(-1, below ? I2C_CONF_BELOW_TEMP_POINT : I2C_CONF_OVER_TEMP_POINT);
        if (action < 0) {
            return -1;
        }
    }
    if (action == TEMP_ACTION_SHUTDOWN || action == TEMP_ACTION_STARTUP) {
        int result = snprintf(buffer, buf_size, "T%s%d°C → %s", 
			below ? "<" : ">", temperature, (action == TEMP_ACTION_SHUTDOWN) ? "Shutdown" : "Startup");
        if (result >= 0 && result < buf_size) {
            return result;
        }
    }
    return 0;
}


/**
 * Configure over temperature action
 */
void configure_over_temperature_action(void) {
    char input[INPUT_MAX_LENGTH];
    int oa, ot;
    
    printf("  Choose action for over temperature (0=None, 1=Startup, 2=Shutdown): ");
    if (read_line(input, sizeof(input)) == NULL) {
        return;
    }
    
	oa = input[0] - '0';
    if (oa == TEMP_ACTION_NONE) {
		i2c_set(-1, I2C_CONF_OVER_TEMP_ACTION, 0);
        sleep(2);
    } else if (oa == TEMP_ACTION_SHUTDOWN || oa == TEMP_ACTION_STARTUP) {
        printf("  Input over temperature point (-30~80, value in Celsius degree): ");
        if (read_line(input, sizeof(input)) == NULL) {
            return;
        }
        if (is_valid_integer_in_range(input, -30, 80, &ot)) {
			bool success = true;
			success &= i2c_set(-1, I2C_CONF_OVER_TEMP_ACTION, oa);
			success &= i2c_set(-1, I2C_CONF_OVER_TEMP_POINT, (uint8_t)ot);
            if (success) {
                char action_msg[64] = {0};
				temperature_action_info(false, oa, ot, action_msg, sizeof(action_msg));
                printf("  Over temperature action is set: %s\n", action_msg);
                sleep(2);
            } else {
				printf("  Over temperature action update failed.\n");
				sleep(2);
			}
        } else {
            printf("  Please input integer between -30 and 80...\n");
            sleep(2);
        }
    } else {
        printf("  Please input 0, 1 or 2...\n");
        sleep(2);
    }
}


/**
 * Configure below temperature action
 */
void configure_below_temperature_action(void) {
    char input[INPUT_MAX_LENGTH];
    int ba, bt;
    
    printf("  Choose action for below temperature (0=None, 1=Startup, 2=Shutdown): ");
    if (read_line(input, sizeof(input)) == NULL) {
        return;
    }
	
	ba = input[0] - '0';
    if (ba == TEMP_ACTION_NONE) {
        i2c_set(-1, I2C_CONF_BELOW_TEMP_ACTION, 0);
        sleep(2);
    } else if (ba == TEMP_ACTION_SHUTDOWN || ba == TEMP_ACTION_STARTUP) {
        printf("  Input below temperature point (-30~80, value in Celsius degree): ");
        if (read_line(input, sizeof(input)) == NULL) {
            return;
        }
        if (is_valid_integer_in_range(input, -30, 80, &bt)) {
			bool success = true;
			success &= i2c_set(-1, I2C_CONF_BELOW_TEMP_ACTION, ba);
			success &= i2c_set(-1, I2C_CONF_BELOW_TEMP_POINT, (uint8_t)bt);
            if (success) {
                char action_msg[64] = {0};
				temperature_action_info(true, ba, bt, action_msg, sizeof(action_msg));
                printf("  Below temperature action is set: %s\n", action_msg);
                sleep(2);
            } else {
				printf("  Below temperature action update failed.\n");
				sleep(2);
			}
        } else {
            printf("  Please input integer between -30 and 80...\n");
            sleep(2);
        }
    } else {
        printf("  Please input 0, 1 or 2...\n");
        sleep(2);
    }
}


/**
 * Display and process menu for other settings
 */
void other_settings(void) {
    printf("  Other Settings:\n");
    
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return;
    }
    
    // [1] Default state when powered
    uint8_t dod = i2c_get(i2c_dev, I2C_CONF_DEFAULT_ON_DELAY);
    printf("  [ 1] Default state when powered");
    if (dod == 255) {
        printf(" [default OFF]\n");
    } else {
        printf(" [default ON with %d seconds delay]\n", dod);
    }
    
    // [2] Power cut delay after shutdown
    uint8_t pcd = i2c_get(i2c_dev, I2C_CONF_POWER_CUT_DELAY);
    printf("  [ 2] Power cut delay after shutdown [%d Seconds]\n", pcd);
    
    // [3] Pulsing interval during sleep
    uint8_t pi = i2c_get(i2c_dev, I2C_CONF_PULSE_INTERVAL);
    printf("  [ 3] Pulsing interval during sleep [%d Seconds]\n", pi);
    
    // [4] White LED pulse length
    uint8_t led = i2c_get(i2c_dev, I2C_CONF_BLINK_LED);
    printf("  [ 4] White LED pulse length [%d ms]\n", led);
    
    // [5] Dummy load pulse length
    uint8_t dload = i2c_get(i2c_dev, I2C_CONF_DUMMY_LOAD);
    printf("  [ 5] Dummy load pulse length [%d ms]\n", dload);
    
    // [6] V-USB adjustment
    uint8_t vusbAdj = i2c_get(i2c_dev, I2C_CONF_ADJ_VUSB);
    float vusbAdj_float = (float)(int8_t)vusbAdj / 100.0f;
    printf("  [ 6] V-USB adjustment [%+.2fV]\n", vusbAdj_float);
    
    // [7] V-IN adjustment
    uint8_t vinAdj = i2c_get(i2c_dev, I2C_CONF_ADJ_VIN);
    float vinAdj_float = (float)(int8_t)vinAdj / 100.0f;
    printf("  [ 7] V-IN  adjustment [%+.2fV]\n", vinAdj_float);
    
    // [8] V-OUT adjustment
    uint8_t voutAdj = i2c_get(i2c_dev, I2C_CONF_ADJ_VOUT);
    float voutAdj_float = (float)(int8_t)voutAdj / 100.0f;
    printf("  [ 8] V-OUT adjustment [%+.2fV]\n", voutAdj_float);
    
    // [9] I-OUT adjustment
    uint8_t ioutAdj = i2c_get(i2c_dev, I2C_CONF_ADJ_IOUT);
    float ioutAdj_float = (float)(int8_t)ioutAdj / 100.0f;
    printf("  [ 9] I-OUT adjustment [%+.3fA]\n", ioutAdj_float);
	
	// [10] Power source priority
    uint8_t psp = i2c_get(i2c_dev, I2C_CONF_PS_PRIORITY);
    printf("  [10] Power source priority [%s first]\n", psp ? "V-IN" : "V-USB");
	
	// [11] Watchdog
    uint8_t wdg = i2c_get(i2c_dev, I2C_CONF_WATCHDOG);
	if (wdg) {
		printf("  [11] Watchdog [Enabled, allow %d missing heartbeats]\n", wdg);
	} else {
		printf("  [11] Watchdog [Disabled]\n");
	}
	
	// [12] Log to file
    uint8_t ltf = i2c_get(i2c_dev, I2C_CONF_LOG_TO_FILE);
	printf("  [12] Log to file on Witty Pi [%s]\n", ltf ? "Yes" : "No");
	
	// [13] Return to main menu
    printf("  [13] Return to main menu\n");
    
    close_i2c_device(i2c_dev);
    
    int optionCount = 13;
    
    printf("  Please input 1~%d: ", optionCount);
	int value;
	bool valid;
	if (!input_number(1, optionCount, &value, &valid, 2)) {
		printf("\n");
		usleep(100000);
		return;	// Empty input, return to main menu
	}
	
	if (!valid) {
		printf("\n");
        usleep(1000000);
		other_settings();
		return;	// Invalid input, retry
    }
    
	int input;
    switch (value) {
        case 1:	// Default state when powered
			if (request_input_number("Input the delay (in second) to turn on Raspberry Pi after getting powered (255=off): ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_DEFAULT_ON_DELAY, input);
				if (input == 255) {
					printf("  Auto power-on is disabled!\n");
				} else {
					printf("  Auto power-on delay is set to %d seconds!\n", input);
				}
			} else {
				other_settings();
			}
            break;
        case 2:	// Power cut delay after shutdown
			if (request_input_number("Input the delay (in second) to cut Raspberry Pi's power after shutdown: ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_POWER_CUT_DELAY, input);
				printf("  Power cut delay is set to %d seconds!\n", input);
			} else {
				other_settings();
			}
            break;
        case 3:	// Pulsing interval during sleep
            if (request_input_number("Input the interval in seconds, for pulsing LED and dummy load: ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_PULSE_INTERVAL, input);
				printf("  Pulsing interval is set to %d seconds!\n", input);
			} else {
				other_settings();
			}
            break;
        case 4:	// White LED pulse length
            if (request_input_number("Input the pulse length (in ms) for LED: ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_BLINK_LED, input);
				printf("  LED blink duration is set to %d ms!\n", input);
			} else {
				other_settings();
			}
            break;
        case 5:	// Dummy load pulse length
            if (request_input_number("Input the pulse length (in ms) for dummy load: ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_DUMMY_LOAD, input);
				printf("  Dummy load active duration is set to %d ms!\n", input);
			} else {
				other_settings();
			}
            break;
        case 6:	// V-USB adjustment
            if (request_input_number("Input the adjust value (in 0.01V) for measured V-USB (-127~127): ", -127, 127, &input, 2)) {
				i2c_set(-1, I2C_CONF_ADJ_VUSB, input);
				printf("  V-USB adjust value is set to %+.2fV!\n", (float)input / 100.0f);
			} else {
				other_settings();
			}
            break;
        case 7:	// V-IN adjustment
			if (request_input_number("Input the adjust value (in 0.01V) for measured V-IN (-127~127): ", -127, 127, &input, 2)) {
				i2c_set(-1, I2C_CONF_ADJ_VIN, input);
				printf("  V-IN adjust value is set to %+.2fV!\n", (float)input / 100.0f);
			} else {
				other_settings();
			}
            break;
        case 8:	// V-OUT adjustment
			if (request_input_number("Input the adjust value (in 0.01V) for measured V-OUT (-127~127): ", -127, 127, &input, 2)) {
				i2c_set(-1, I2C_CONF_ADJ_VOUT, input);
				printf("  V-OUT adjust value is set to %+.2fV!\n", (float)input / 100.0f);
			} else {
				other_settings();
			}
            break;
		case 9:	// I-OUT adjustment
			if (request_input_number("Input the adjust value (in 0.001A) for measured I-OUT (-127~127): ", -127, 127, &input, 2)) {
				i2c_set(-1, I2C_CONF_ADJ_IOUT, input);
				printf("  I-OUT adjust value is set to %+.3fA!\n", (float)input / 1000.0f);
			} else {
				other_settings();
			}
            break;
		case 10:// Power source priority
			if (request_input_number("Specify the power source with higher priority (0=V-USB, 1=V-IN): ", 0, 1, &input, 2)) {
				i2c_set(-1, I2C_CONF_PS_PRIORITY, input);
				printf("  %s is set to have higher priority!\n", input ? "V-IN" : "V-USB");
			} else {
				other_settings();
			}
			break;
		case 11:// Watchdog
			if (request_input_number("Input the number of allowed missing heartbeats for watchdog (0~255, 0=Disabled): ", 0, 255, &input, 2)) {
				i2c_set(-1, I2C_CONF_WATCHDOG, input);
				if (input) {
					printf("  Watchdog is enabled with %d allow missing heartbeats!\n", input);
				} else {
					printf("  Watchdog is disabled!\n");
				}
			} else {
				other_settings();
			}
			break;
		case 12:// Log to file
			if (request_input_number("Specify whether to write log file on Witty Pi (0=No, 1=Yes): ", 0, 1, &input, 2)) {
				i2c_set(-1, I2C_CONF_LOG_TO_FILE, input);
				printf("  %s log file on Witty Pi!\n", input ? "Write" : "Do not write");
			} else {
				other_settings();
			}
			break;
		case 13:// Return to main menu
            return;
        default:
            break;
    }
}


/**
 * Display and process menu for resetting data
 */
void reset_data(void) {
	printf("  [ 1] Clear scheduled startup time\n");
	printf("  [ 2] Clear scheduled shutdown time\n");
	printf("  [ 3] Stop using schedule script\n");
	printf("  [ 4] Clear low-voltage threshold\n");
	printf("  [ 5] Clear recovery-voltage threshold\n");
	printf("  [ 6] Clear over-temperature action\n");
	printf("  [ 7] Clear below-temperature action\n");
	printf("  [ 8] Reset all configuration values\n");
	printf("  [ 9] Perform all actions above\n");
	printf("  [10] Return to main menu\n");
	
	int optionCount = 10;
	printf("  Please input 1~%d: ", optionCount);
	int value;
	bool valid;
	if (!input_number(1, optionCount, &value, &valid, 2)) {
		printf("\n");
		usleep(100000);
		return;	// Empty input, return to main menu
	}
	
	if (!valid) {
		printf("\n");
        usleep(1000000);
		reset_data();
		return;	// Invalid input, retry
    }
    
    switch (value) {
        case 1:	// Clear scheduled startup time
			clear_startup_time();
			printf("  Scheduled startup time is cleared!\n");
            return;
        case 2:	// Clear scheduled shutdown time
			clear_shutdown_time();
			printf("  Scheduled shutdown time is cleared!\n");
            return;
		case 3:	// Stop using schedule script
			run_admin_command(I2C_ADMIN_PWD_CMD_PURGE_SCRIPT);
			return;
		case 4:	// Clear low-voltage threshold
			i2c_set(-1, I2C_CONF_LOW_VOLTAGE, 0);
			printf("  Low-voltage threshold is cleared!\n");
            return;
		case 5:	// Clear recovery-voltage threshold
			i2c_set(-1, I2C_CONF_RECOVERY_VOLTAGE, 0);
			printf("  Recovery-voltage threshold is cleared!\n");
            return;
		case 6: // Clear over-temperature action
			i2c_set(-1, I2C_CONF_OVER_TEMP_ACTION, 0);
			printf("  Over-temperature action is cleared!\n");
            return;
		case 7: // Clear below-temperature action
			i2c_set(-1, I2C_CONF_BELOW_TEMP_ACTION, 0);
			printf("  Below-temperature action is cleared!\n");
            return;
        case 8: // Reset all configuration values
            run_admin_command(I2C_ADMIN_PWD_CMD_RESET_CONF);
            printf("  All configuration values are reset!\n");
            return;
		case 9: // Perform all actions above
			clear_startup_time();
			clear_shutdown_time();
			run_admin_command(I2C_ADMIN_PWD_CMD_PURGE_SCRIPT);
			int i2c_dev = open_i2c_device();
			if (i2c_dev >= 0) {
				i2c_set(i2c_dev, I2C_CONF_LOW_VOLTAGE, 0);
				i2c_set(i2c_dev, I2C_CONF_RECOVERY_VOLTAGE, 0);
				i2c_set(i2c_dev, I2C_CONF_OVER_TEMP_ACTION, 0);
				i2c_set(i2c_dev, I2C_CONF_BELOW_TEMP_ACTION, 0);
				close_i2c_device(i2c_dev);
			}
			run_admin_command(I2C_ADMIN_PWD_CMD_RESET_CONF);
			printf("  All cleared!\n");
			return;
		case 10: // Return to main menu
			return;
	}
	reset_data();
}


/**
 * Display and process menu for administration
 */
void administrate(void) {
	printf("  [1] Print product information in log\n");
	printf("  [2] Format Witty Pi disk\n");
	printf("  [3] Reset RTC\n");
	printf("  [4] Turn on/off ID EEPROM write protection\n");
	printf("  [5] Synchronize configuration to file\n");
	printf("  [6] Save log to file\n");
	printf("  [7] Load and generate schedule scripts\n");
	printf("  [8] Return to main menu\n");

	int optionCount = 8;
	printf("  Please input 1~%d: ", optionCount);
	int value;
	bool valid;
	if (!input_number(1, optionCount, &value, &valid, 2)) {
		printf("\n");
		usleep(100000);
		return;	// Empty input, return to main menu
	}
	
	if (!valid) {
		printf("\n");
        usleep(1000000);
		administrate();
		return;	// Invalid input, retry
    }
    
    switch (value) {
        case 1:	// Print product information in log
			run_admin_command(I2C_ADMIN_PWD_CMD_PRINT_PRODUCT_INFO);
			printf("  Product information is printed!\n\n");
            break;
        case 2:	// Format Witty Pi disk
			if (user_confirm("All data on Witty Pi disk will be erased! Are you sure?", 2)) {
				run_admin_command(I2C_ADMIN_PWD_CMD_FORMAT_DISK);
				printf("  Witty Pi disk is formatted!\n\n");
			} else {
				printf("  Task is cancelled.\n\n");
			}
            break;
		case 3: // Reset RTC
			if (user_confirm("Do you want to reset the RTC?", 2)) {
				run_admin_command(I2C_ADMIN_PWD_CMD_RESET_RTC);
				printf("  RTC is reset!\n\n");
			} else {
				printf("  Task is cancelled.\n\n");
			}
            break;
		case 4: // Turn on/off ID EEPROM write protection
			printf("  How to set the ID EEPROM write protection? (1=ON, 0=OFF): ");
			int value;
			bool valid;
			if (!input_number(0, 1, &value, &valid, 2) || !valid) {
				usleep(100000);
				printf("\n");
				break;
			}
			if (value) {
				run_admin_command(I2C_ADMIN_PWD_CMD_ENABLE_ID_EEPROM_WP);
				printf("  ID EEPROM write protection is ON.\n\n");
			} else {
				run_admin_command(I2C_ADMIN_PWD_CMD_DISABLE_ID_EEPROM_WP);
				printf("  ID EEPROM write protection is OFF.\n\n");
			}
			break;
		case 5: // Synchronize configuration to file
			run_admin_command(I2C_ADMIN_PWD_CMD_SYNC_CONF);
			printf("  Configuration is synchronized to file on Witty Pi.\n\n");
			break;
		case 6: // Save log to file
			run_admin_command(I2C_ADMIN_PWD_CMD_SAVE_LOG);
			printf("  Log is saved to file on Witty Pi.\n\n");
			break;
		case 7: // Load and generate schedule scripts
			run_admin_command(I2C_ADMIN_PWD_CMD_LOAD_SCRIPT);
			printf("  Log schedule.wpi and generate .act and .skd files.\n\n");
			break;
		case 8: // Return to main menu
			return;
	}
	administrate();
}


/**
 * Display and process the main menu
 */
void do_main_menu(void) {
    printf("  1. Write system time to RTC\n");
    printf("  2. Write RTC time to system\n");
    printf("  3. Synchronize with network time\n");
    printf("  4. Schedule next shutdown");
	uint8_t date, hour, minute, second;
	if (get_shutdown_time(&date, &hour, &minute, &second)) {
		printf(" [%02d %02d:%02d:%02d]", date, hour, minute, second);
	}
	printf("\n");
    printf("  5. Schedule next startup ");
	if (get_startup_time(&date, &hour, &minute, &second)) {
		printf(" [%02d %02d:%02d:%02d]", date, hour, minute, second);
	}
	printf("\n");
    printf("  6. Choose schedule script%s\n", is_script_in_use() ? " (in use)" : "");
    printf("  7. Set low voltage threshold");
	float lv = get_low_voltage_threshold();
	if (lv > 0.01f) {
		printf(" [%.1fV]", lv);
	}
	printf("\n");
    printf("  8. Set recovery voltage threshold");
	float rv = get_recovery_voltage_threshold();
	if (rv > 0.01f) {
		printf(" [%.1fV]", rv);
	}
	printf("\n");
    printf("  9. Set over temperature action");
	char buf[32];
	if (temperature_action_info(false, -1, -1, buf, 32) > 0) {
		printf("  [%s]", buf);
	}
	printf("\n");
    printf(" 10. Set below temperature action");
	if (temperature_action_info(true, -1, -1, buf, 32) > 0) {
		printf(" [%s]", buf);
	}
	printf("\n");
    printf(" 11. Other settings...\n");
	printf(" 12. Reset data...\n");
	printf(" 13. Administrate...\n");
    printf(" 14. Exit\n");
    printf(" Please input 1~14: ");
    
	int value;
	bool valid;
	if (!input_number(1, 14, &value, &valid, 1)) {
		usleep(100000);
		printf("\n");
		return;	// Empty input, do nothing
	}
    
    if (!valid) {
        usleep(1000000);
    } else {
        switch (value) {
            case 1:	// Write system time to RTC
				if (system_to_rtc()) {
					printf("System -> RTC OK\n");
				} else {
					printf("Write failed\n");
				}
                break;
            case 2:	// Write RTC time to system
				if (rtc_to_system()) {
					printf("RTC -> System OK\n");
				} else {
					printf("Write failed\n");
				}
                break;
            case 3:	// Synchronize with network time
				if (network_to_system_and_rtc()) {
					printf("Network -> System -> RTC OK\n");
				} else {
					printf("Synchronization failed\n");
				}
                break;
            case 4:	// Schedule next shutdown
				schedule_shutdown();
                break;
            case 5:	// Schedule next startup
				schedule_startup();
                break;
            case 6:	// Choose schedule script
                choose_schedule_script();
                break;
            case 7: // Set low voltage threshold
				configure_low_voltage_threshold();
                break;
            case 8:	// Set recovery voltage threshold
				configure_recovery_voltage_threshold();
                break;
            case 9:	// Set over temperature action
				configure_over_temperature_action();
                break;
            case 10:// Set below temperature action
				configure_below_temperature_action();
                break;
            case 11:// Other settings...
                other_settings();
                break;
			case 12:// Reset data...
				reset_data();
                break;
            case 13:// Administrate...
				administrate();
                break;
            case 14:// Exit
                handle_signal(2);
                break;
        }
    }
}


/**
 * Main function
 */
int main(int argc, char *argv[]) {
    
    
    // Process --debug arguments
    bool debug = false;
    for (int i = 1; i < argc; i ++) {
        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
        }
    }
    set_log_mode(debug ? LOG_WITH_TIME : LOG_NONE);
    
    // Register signal handler
    signal(SIGINT, handle_signal);

    // Print the banner
    printf("================================================================================\n");
    printf("|                                                                              |\n");
    printf("|   Witty Pi - Realtime Clock + Power Management for Raspberry Pi              |\n");
    printf("|                                                                              |\n");
    printf("|            < Version %s >    by Dun Cat B.V. (UUGear)                     |\n", SOFTWARE_VERSION_STR);
    printf("|                                                                              |\n");
    printf("================================================================================\n");

    // Main loop
    while (running) {
        do_info_bar();
        do_main_menu();
    }
}