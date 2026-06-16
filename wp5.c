#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <regex.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/file.h>
#include <fcntl.h>

#include "wp5lib.h"

#define INPUT_MAX_LENGTH                32
#define PATH_INPUT_MAX_LENGTH           256
#define DOWNLOAD_BUFFER_SIZE            8192
#define MAX_FILENAME_LENGTH             64
#define LOCAL_FILE_LIST_MAX             128
#define REMOTE_FILE_LIST_MAX            128
#define MAX_CHUNK_CONTENT               4000

#define FW_FILE_ADMIN_REQUIRED_MAJOR        1
#define FW_FILE_ADMIN_REQUIRED_MINOR        2
#define FW_VIN_HOT_STANDBY_REQUIRED_MAJOR   1
#define FW_VIN_HOT_STANDBY_REQUIRED_MINOR   5

#define SCRIPT_ACTIVATION_SETTLE_MS     25000
#define SCRIPT_ACTIVATION_POLL_MS       250
#define DEVICE_READY_WAIT_MS            2000

#define IN_USE_SCRIPT_NAME              "schedule"

#define WP5_APP_LOCK_FILE "/var/lock/wittypi5_app.lock"


typedef struct {    // The validity of scheduled startup/shutdown time
    bool valid;
    uint8_t date;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} ScheduledTimeState;


bool running = true;

int model = MODEL_UNKNOWN;

static int wp5_app_lock_fd = -1;

static bool acquire_wp5_app_lock(void) {
    wp5_app_lock_fd = open(WP5_APP_LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (wp5_app_lock_fd < 0) {
        return false;
    }

    if (flock(wp5_app_lock_fd, LOCK_EX | LOCK_NB) < 0) {
        close(wp5_app_lock_fd);
        wp5_app_lock_fd = -1;
        return false;
    }

    return true;
}

static void release_wp5_app_lock(void) {
    if (wp5_app_lock_fd >= 0) {
        flock(wp5_app_lock_fd, LOCK_UN);
        close(wp5_app_lock_fd);
        wp5_app_lock_fd = -1;
    }
}


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


static bool read_startup_state(ScheduledTimeState *state) {
    if (state == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->valid = get_startup_time(&state->date, &state->hour, &state->minute, &state->second);
    return true;
}


static bool read_shutdown_state(ScheduledTimeState *state) {
    if (state == NULL) {
        return false;
    }
    memset(state, 0, sizeof(*state));
    state->valid = get_shutdown_time(&state->date, &state->hour, &state->minute, &state->second);
    return true;
}


static bool scheduled_time_equals(const ScheduledTimeState *left, const ScheduledTimeState *right) {
    if (left == NULL || right == NULL) {
        return false;
    }
    if (left->valid != right->valid) {
        return false;
    }
    if (!left->valid) {
        return true;
    }
    return left->date == right->date &&
           left->hour == right->hour &&
           left->minute == right->minute &&
           left->second == right->second;
}


static bool wait_for_wittypi_ready(int timeout_ms) {
    int waited_ms = 0;
    while (waited_ms <= timeout_ms) {
        if (get_wittypi_model() != MODEL_UNKNOWN) {
            return true;
        }
        usleep(300000);
        waited_ms += 300;
    }
    return false;
}


static void print_scheduled_time_line(const char *label, const ScheduledTimeState *state) {
    if (label == NULL || state == NULL || !state->valid) {
        return;
    }
    printf("  %s: %02u %02u:%02u:%02u\n",
           label, state->date, state->hour, state->minute, state->second);
}


static bool run_admin_command_expect_ok(uint16_t psw_cmd, const char *action_name) {
    uint8_t status = ADMIN_STATUS_UNKNOWN;
    if (!run_admin_command_wait(psw_cmd, &status)) {
        printf("  Failed to %s: no response from firmware.\n", action_name);
        return false;
    }
    if (status != ADMIN_STATUS_OK) {
        printf("  Failed to %s: admin status %u.\n", action_name, status);
        return false;
    }
    return true;
}


static bool wait_for_schedule_activation_result(bool before_in_use,
                                                const ScheduledTimeState *before_startup,
                                                const ScheduledTimeState *before_shutdown,
                                                ScheduledTimeState *after_startup,
                                                ScheduledTimeState *after_shutdown,
                                                bool *after_in_use) {
    int waited_ms = 0;
    while (waited_ms <= SCRIPT_ACTIVATION_SETTLE_MS) {
        bool current_in_use = is_script_in_use();
        ScheduledTimeState current_startup = {0};
        ScheduledTimeState current_shutdown = {0};
        read_startup_state(&current_startup);
        read_shutdown_state(&current_shutdown);

        bool startup_changed = !scheduled_time_equals(before_startup, &current_startup);
        bool shutdown_changed = !scheduled_time_equals(before_shutdown, &current_shutdown);

        if (waited_ms >= 1000) {
            if (current_startup.valid || current_shutdown.valid) {
                if (after_startup != NULL) {
                    *after_startup = current_startup;
                }
                if (after_shutdown != NULL) {
                    *after_shutdown = current_shutdown;
                }
                if (after_in_use != NULL) {
                    *after_in_use = current_in_use;
                }
                return true;
            }

            if (!current_in_use && (!before_in_use || startup_changed || shutdown_changed || waited_ms >= 3000)) {
                if (after_startup != NULL) {
                    *after_startup = current_startup;
                }
                if (after_shutdown != NULL) {
                    *after_shutdown = current_shutdown;
                }
                if (after_in_use != NULL) {
                    *after_in_use = current_in_use;
                }
                return false;
            }
        }

        usleep((useconds_t)SCRIPT_ACTIVATION_POLL_MS * 1000);
        waited_ms += SCRIPT_ACTIVATION_POLL_MS;
    }

    if (after_startup != NULL) {
        read_startup_state(after_startup);
    }
    if (after_shutdown != NULL) {
        read_shutdown_state(after_shutdown);
    }
    if (after_in_use != NULL) {
        *after_in_use = is_script_in_use();
    }
    return (after_startup != NULL && after_startup->valid) ||
           (after_shutdown != NULL && after_shutdown->valid);
}


static bool firmware_version_at_least(int required_major, int required_minor) {
    int major = 0;
    int minor = 0;
    if (!get_firmware_version(&major, &minor)) {
        return false;
    }
    if (major > required_major) {
        return true;
    }
    return major == required_major && minor >= required_minor;
}


static bool firmware_supports_file_management(void) {
    return firmware_version_at_least(FW_FILE_ADMIN_REQUIRED_MAJOR,
                                     FW_FILE_ADMIN_REQUIRED_MINOR);
}


static bool firmware_supports_vin_hot_standby(void) {
    return firmware_version_at_least(FW_VIN_HOT_STANDBY_REQUIRED_MAJOR,
                                     FW_VIN_HOT_STANDBY_REQUIRED_MINOR);
}


static void print_requires_new_firmware_message(void) {
    printf("  This feature requires firmware %d.%d or later.\n",
           FW_FILE_ADMIN_REQUIRED_MAJOR, FW_FILE_ADMIN_REQUIRED_MINOR);
}


static const char *admin_status_text(uint8_t status) {
    switch (status) {
        case ADMIN_STATUS_OK:
            return "OK";
        case ADMIN_STATUS_FILE_NOT_FOUND:
            return "File not found";
        case ADMIN_STATUS_CANNOT_DELETE_ACTIVE:
            return "Can not delete the active schedule script";
        case ADMIN_STATUS_IO_ERROR:
            return "I/O error";
        case ADMIN_STATUS_INVALID_PACKET:
            return "Invalid packet";
        case ADMIN_STATUS_FILE_TOO_LARGE:
            return "File too large";
        case ADMIN_STATUS_INVALID_DIRECTORY:
            return "Invalid directory";
        case ADMIN_STATUS_BUSY:
            return "Device is busy";
        default:
            return "Unknown status";
    }
}


static int compare_names(const void *a, const void *b) {
    const char *left = (const char *)a;
    const char *right = (const char *)b;
    return strcasecmp(left, right);
}


static bool has_allowed_schedule_extension(const char *filename) {
    if (filename == NULL) {
        return false;
    }
    const char *dot = strrchr(filename, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".wpi") == 0 ||
           strcasecmp(dot, ".act") == 0 ||
           strcasecmp(dot, ".skd") == 0;
}


static int scan_local_schedule_files(char files[][MAX_FILENAME_LENGTH], int max_files) {
    DIR *dir = opendir(".");
    if (dir == NULL) {
        return 0;
    }

    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (!has_allowed_schedule_extension(entry->d_name)) {
            continue;
        }

        struct stat st;
        if (stat(entry->d_name, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        snprintf(files[count], MAX_FILENAME_LENGTH, "%s", entry->d_name);
        count++;
        if (count >= max_files) {
            break;
        }
    }
    closedir(dir);

    if (count > 1) {
        qsort(files, (size_t)count, sizeof(files[0]), compare_names);
    }
    return count;
}


static bool pack_filename_hex(const char *filename, uint8_t *output, size_t output_size, size_t *packet_len) {
    if (filename == NULL || output == NULL || output_size < 5) {
        return false;
    }

    size_t filename_len = strlen(filename);
    if (filename_len == 0 || filename_len + 5 > output_size) {
        return false;
    }

    output[0] = PACKET_BEGIN;
    memcpy(output + 1, filename, filename_len);
    output[filename_len + 1] = PACKET_DELIMITER;

    uint8_t crc = calculate_crc8(output, filename_len + 2);
    static const char hex[] = "0123456789ABCDEF";
    output[filename_len + 2] = (uint8_t)hex[(crc >> 4) & 0x0F];
    output[filename_len + 3] = (uint8_t)hex[crc & 0x0F];
    output[filename_len + 4] = PACKET_END;

    if (packet_len) {
        *packet_len = filename_len + 5;
    }
    return true;
}


static bool pack_upload_packet(const char *filename, const uint8_t *content, size_t content_len,
                               uint8_t *output, size_t output_size, size_t *packet_len) {
    if (filename == NULL || output == NULL || (content_len > 0 && content == NULL)) {
        return false;
    }

    size_t filename_len = strlen(filename);
    size_t required = 1 + filename_len + 1 + content_len + 1 + 2 + 1;
    if (filename_len == 0 || required > output_size) {
        return false;
    }

    size_t pos = 0;
    output[pos++] = PACKET_BEGIN;
    memcpy(output + pos, filename, filename_len);
    pos += filename_len;
    output[pos++] = PACKET_DELIMITER;
    if (content_len > 0) {
        memcpy(output + pos, content, content_len);
        pos += content_len;
    }
    output[pos++] = PACKET_DELIMITER;

    uint8_t crc = calculate_crc8(output, pos);
    static const char hex[] = "0123456789ABCDEF";
    output[pos++] = (uint8_t)hex[(crc >> 4) & 0x0F];
    output[pos++] = (uint8_t)hex[crc & 0x0F];
    output[pos++] = PACKET_END;

    if (packet_len) {
        *packet_len = pos;
    }
    return true;
}


static bool send_bytes_to_admin_upload(const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return false;
    }
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < len; i++) {
        if (!i2c_set_impl(i2c_dev, I2C_ADMIN_UPLOAD, data[i], false)) {
            ok = false;
            break;
        }
    }
    close_i2c_device(i2c_dev);
    return ok;
}


static bool parse_file_list_packet(const uint8_t *packet, size_t packet_len, bool hide_in_use_script,
                                   char files[][MAX_FILENAME_LENGTH], int max_files, int *out_count) {
    if (out_count) {
        *out_count = 0;
    }
    if (packet == NULL || packet_len < 4 || packet[0] != PACKET_BEGIN || packet[packet_len - 1] != PACKET_END) {
        return false;
    }

    int last_delim = -1;
    for (size_t i = 1; i + 1 < packet_len; i++) {
        if (packet[i] == PACKET_DELIMITER) {
            last_delim = (int)i;
        }
    }
    if (last_delim < 1 || (size_t)(last_delim + 3) != packet_len - 1) {
        return false;
    }

    int hi = isdigit((unsigned char)packet[last_delim + 1]) ? packet[last_delim + 1] - '0' :
             (packet[last_delim + 1] >= 'A' && packet[last_delim + 1] <= 'F') ? packet[last_delim + 1] - 'A' + 10 : -1;
    int lo = isdigit((unsigned char)packet[last_delim + 2]) ? packet[last_delim + 2] - '0' :
             (packet[last_delim + 2] >= 'A' && packet[last_delim + 2] <= 'F') ? packet[last_delim + 2] - 'A' + 10 : -1;
    if (hi < 0 || lo < 0) {
        return false;
    }
    uint8_t crc_expected = (uint8_t)((hi << 4) | lo);
    uint8_t crc_actual = calculate_crc8(packet, (size_t)(last_delim + 1));
    if (crc_expected != crc_actual) {
        return false;
    }

    int count = 0;
    size_t start = 1;
    for (size_t i = 1; i <= (size_t)last_delim; i++) {
        if (packet[i] != PACKET_DELIMITER) {
            continue;
        }
        size_t name_len = i - start;
        if (name_len > 0) {
            char name[MAX_FILENAME_LENGTH];
            size_t copy_len = (name_len >= sizeof(name)) ? sizeof(name) - 1 : name_len;
            memcpy(name, packet + start, copy_len);
            name[copy_len] = '\0';

            if ((!hide_in_use_script || strncasecmp(name, IN_USE_SCRIPT_NAME, strlen(IN_USE_SCRIPT_NAME)) != 0) &&
                count < max_files) {
                snprintf(files[count], MAX_FILENAME_LENGTH, "%s", name);
                count++;
            }
        }
        start = i + 1;
    }

    if (count > 1) {
        qsort(files, (size_t)count, sizeof(files[0]), compare_names);
    }
    if (out_count) {
        *out_count = count;
    }
    return true;
}


static bool get_remote_file_list(uint8_t dir, bool hide_in_use_script,
                                 char files[][MAX_FILENAME_LENGTH], int max_files, int *out_count) {
    uint8_t status = 0xFF;
    if (!i2c_set(-1, I2C_ADMIN_DIR, dir)) {
        return false;
    }
    if (!run_admin_command_wait(I2C_ADMIN_PWD_CMD_LIST_FILES, &status) || status != ADMIN_STATUS_OK) {
        return false;
    }

    uint8_t packet[DOWNLOAD_BUFFER_SIZE];
    int len = i2c_read_stream_util(-1, I2C_ADMIN_DOWNLOAD, packet, DOWNLOAD_BUFFER_SIZE - 1, PACKET_END);
    if (len <= 0) {
        return false;
    }
    return parse_file_list_packet(packet, (size_t)len, hide_in_use_script, files, max_files, out_count);
}


static bool read_admin_stream_byte(int i2c_dev, uint8_t index, uint8_t *value) {
    int read_value = i2c_get_impl(i2c_dev, index, false);
    if (read_value < 0) {
        return false;
    }
    *value = (uint8_t)read_value;
    return true;
}


static bool read_admin_stream_bytes(int i2c_dev, uint8_t index, uint8_t *buffer, size_t len) {
    if (buffer == NULL) {
        return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (!read_admin_stream_byte(i2c_dev, index, &buffer[i])) {
            return false;
        }
    }
    return true;
}


static int hex4_to_int(const uint8_t *buffer) {
    int value = 0;
    for (int i = 0; i < 4; i++) {
        int nibble = -1;
        if (buffer[i] >= '0' && buffer[i] <= '9') {
            nibble = buffer[i] - '0';
        } else if (buffer[i] >= 'A' && buffer[i] <= 'F') {
            nibble = buffer[i] - 'A' + 10;
        } else if (buffer[i] >= 'a' && buffer[i] <= 'f') {
            nibble = buffer[i] - 'a' + 10;
        }
        if (nibble < 0) {
            return -1;
        }
        value = (value << 4) | nibble;
    }
    return value;
}


static bool read_download_chunk_packet(int i2c_dev, uint8_t *content, int max_content, int *content_len) {
    uint8_t header[6];
    bool got_header = false;

    for (int attempt = 0; attempt < 80; attempt++) {
        if (!read_admin_stream_bytes(i2c_dev, I2C_ADMIN_DOWNLOAD, header, sizeof(header))) {
            return false;
        }

        bool all_zero = true;
        for (size_t i = 0; i < sizeof(header); i++) {
            if (header[i] != 0) {
                all_zero = false;
                break;
            }
        }
        if (all_zero) {
            usleep(10000);
            continue;
        }
        if (header[0] != PACKET_BEGIN || header[5] != PACKET_DELIMITER) {
            return false;
        }
        got_header = true;
        break;
    }

    if (!got_header) {
        return false;
    }

    int chunk_len = hex4_to_int(&header[1]);
    if (chunk_len < 0 || chunk_len > max_content) {
        return false;
    }

    size_t tail_len = (size_t)chunk_len + 4;
    uint8_t tail[MAX_CHUNK_CONTENT + 4];
    if (!read_admin_stream_bytes(i2c_dev, I2C_ADMIN_DOWNLOAD, tail, tail_len)) {
        return false;
    }
    if (tail[chunk_len] != PACKET_DELIMITER || tail[chunk_len + 3] != PACKET_END) {
        return false;
    }

    uint8_t packet[MAX_CHUNK_CONTENT + 10];
    memcpy(packet, header, sizeof(header));
    memcpy(packet + sizeof(header), tail, tail_len);

    int hi = (tail[chunk_len + 1] >= '0' && tail[chunk_len + 1] <= '9') ? tail[chunk_len + 1] - '0' :
             (tail[chunk_len + 1] >= 'A' && tail[chunk_len + 1] <= 'F') ? tail[chunk_len + 1] - 'A' + 10 : -1;
    int lo = (tail[chunk_len + 2] >= '0' && tail[chunk_len + 2] <= '9') ? tail[chunk_len + 2] - '0' :
             (tail[chunk_len + 2] >= 'A' && tail[chunk_len + 2] <= 'F') ? tail[chunk_len + 2] - 'A' + 10 : -1;
    if (hi < 0 || lo < 0) {
        return false;
    }

    uint8_t crc_expected = (uint8_t)((hi << 4) | lo);
    uint8_t crc_actual = calculate_crc8(packet, (size_t)(chunk_len + 7));
    if (crc_expected != crc_actual) {
        return false;
    }

    if (chunk_len > 0 && content != NULL) {
        memcpy(content, packet + 6, (size_t)chunk_len);
    }
    if (content_len) {
        *content_len = chunk_len;
    }
    return true;
}


static bool start_remote_download(uint8_t dir, const char *remote_filename, uint8_t *status) {
    uint8_t packet[MAX_FILENAME_LENGTH + 8];
    size_t packet_len = 0;
    if (!pack_filename_hex(remote_filename, packet, sizeof(packet), &packet_len)) {
        return false;
    }
    if (!i2c_set(-1, I2C_ADMIN_DIR, dir)) {
        return false;
    }
    if (!send_bytes_to_admin_upload(packet, packet_len)) {
        return false;
    }
    usleep(1000);
    return run_admin_command_wait(I2C_ADMIN_PWD_CMD_FILE_DOWNLOAD, status);
}


static bool download_remote_file(uint8_t dir, const char *remote_filename, const char *local_path) {
    uint8_t status = ADMIN_STATUS_UNKNOWN;
    if (!start_remote_download(dir, remote_filename, &status) || status != ADMIN_STATUS_OK) {
        if (status == ADMIN_STATUS_INVALID_PACKET) {
            printf("  Retrying...\n");
            usleep(10000);
            if (!start_remote_download(dir, remote_filename, &status) || status != ADMIN_STATUS_OK) {
                printf("  Download failed: %s\n", admin_status_text(status));
                return false;
            }
        } else {
            printf("  Download failed: %s\n", admin_status_text(status));
            return false;
        }
    }

    FILE *fp = fopen(local_path, "wb");
    if (fp == NULL) {
        printf("  Failed to open %s for writing: %s\n", local_path, strerror(errno));
        return false;
    }

    bool ok = true;
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        fclose(fp);
        return false;
    }

    int chunk_index = 0;
    size_t total_bytes = 0;
    while (true) {
        uint8_t content[MAX_CHUNK_CONTENT];
        int content_len = 0;
        if (!read_download_chunk_packet(i2c_dev, content, sizeof(content), &content_len)) {
            ok = false;
            break;
        }
        if (content_len == 0) {
            break;
        }
        if (fwrite(content, 1, (size_t)content_len, fp) != (size_t)content_len) {
            ok = false;
            break;
        }

        chunk_index++;
        total_bytes += (size_t)content_len;
        printf("\r  Downloading %s: chunk %d, %zu bytes", remote_filename, chunk_index, total_bytes);
        fflush(stdout);

        if (!run_admin_command_wait(I2C_ADMIN_PWD_CMD_FILE_DOWNLOAD_NEXT, &status) || status != ADMIN_STATUS_OK) {
            ok = false;
            break;
        }
    }

    if (chunk_index > 0) {
        printf("\n");
    }

    close_i2c_device(i2c_dev);
    fclose(fp);
    if (!ok) {
        remove(local_path);
    }
    return ok;
}


static bool rename_existing_file_with_mtime(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) {
        return (errno == ENOENT);
    }

    struct tm tm_info;
    localtime_r(&st.st_mtime, &tm_info);
    char ts[32];
    if (strftime(ts, sizeof(ts), "%Y%m%d%H%M%S", &tm_info) == 0) {
        return false;
    }

    const char *dot = strrchr(path, '.');
    char renamed[PATH_INPUT_MAX_LENGTH * 2];
    if (dot != NULL && dot != path) {
        size_t base_len = (size_t)(dot - path);
        snprintf(renamed, sizeof(renamed), "%.*s.%s%s", (int)base_len, path, ts, dot);
    } else {
        snprintf(renamed, sizeof(renamed), "%s.%s", path, ts);
    }

    if (rename(path, renamed) != 0) {
        return false;
    }

    printf("  %s already exists, renamed to %s\n", path, renamed);
    return true;
}


static bool upload_schedule_file(const char *local_filename) {
    if (!has_allowed_schedule_extension(local_filename)) {
        printf("  Only .wpi, .act and .skd files are allowed.\n");
        return false;
    }

    struct stat st;
    if (stat(local_filename, &st) != 0 || !S_ISREG(st.st_mode)) {
        printf("  File not found in current directory: %s\n", local_filename);
        return false;
    }
    if (st.st_size > MAX_CHUNK_CONTENT) {
        printf("  The file is too large (%ld bytes). The firmware limit is %d bytes.\n",
               (long)st.st_size, MAX_CHUNK_CONTENT);
        return false;
    }

    FILE *fp = fopen(local_filename, "rb");
    if (fp == NULL) {
        printf("  Failed to open %s: %s\n", local_filename, strerror(errno));
        return false;
    }

    uint8_t content[MAX_CHUNK_CONTENT];
    size_t content_len = fread(content, 1, sizeof(content), fp);
    fclose(fp);
    if ((long)content_len != st.st_size) {
        printf("  Failed to read the whole file.\n");
        return false;
    }

    for (size_t i = 0; i < content_len; i++) {
        if (content[i] == PACKET_BEGIN || content[i] == PACKET_DELIMITER || content[i] == PACKET_END) {
            printf("  The file contains reserved protocol characters and can not be uploaded.\n");
            return false;
        }
        if (content[i] == 0) {
            printf("  The file contains NUL bytes and can not be uploaded safely.\n");
            return false;
        }
    }

    uint8_t packet[1 + MAX_FILENAME_LENGTH + 1 + MAX_CHUNK_CONTENT + 1 + 2 + 1];
    size_t packet_len = 0;
    if (!pack_upload_packet(local_filename, content, content_len, packet, sizeof(packet), &packet_len)) {
        printf("  Failed to build upload packet.\n");
        return false;
    }

    if (!i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE) || !send_bytes_to_admin_upload(packet, packet_len)) {
        printf("  Failed to send the upload packet.\n");
        return false;
    }

    uint8_t status = 0xFF;
    if (!run_admin_command_wait(I2C_ADMIN_PWD_CMD_FILE_UPLOAD, &status) || status != ADMIN_STATUS_OK) {
        printf("  Upload failed: %s\n", admin_status_text(status));
        return false;
    }
    return true;
}


static bool activate_schedule_script_modern(const char *remote_filename) {
    ScheduledTimeState before_startup = {0};
    ScheduledTimeState before_shutdown = {0};
    bool before_in_use = is_script_in_use();
    read_startup_state(&before_startup);
    read_shutdown_state(&before_shutdown);

    uint8_t packet[MAX_FILENAME_LENGTH + 8];
    size_t packet_len = 0;
    if (!pack_filename_hex(remote_filename, packet, sizeof(packet), &packet_len)) {
        return false;
    }
    if (!i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE) || !send_bytes_to_admin_upload(packet, packet_len)) {
        printf("  Failed to send activation request.\n");
        return false;
    }

    uint8_t status = 0xFF;
    if (!run_admin_command_wait(I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT, &status) || status != ADMIN_STATUS_OK) {
        printf("  Activation failed: %s\n", admin_status_text(status));
        return false;
    }

    printf("  Waiting for script processing...");
    fflush(stdout);

    if (!wait_for_wittypi_ready(DEVICE_READY_WAIT_MS)) {
        printf("\n  Warning: Witty Pi did not respond immediately after the activation request.\n");
    }

    ScheduledTimeState after_startup = {0};
    ScheduledTimeState after_shutdown = {0};
    bool after_in_use = false;
    bool success = wait_for_schedule_activation_result(before_in_use, &before_startup, &before_shutdown,
                                                       &after_startup, &after_shutdown, &after_in_use);

    printf("done\n");

    if (!success) {
        printf("  Activation finished, but no valid future action was applied.\n");
        return false;
    }

    if (!after_in_use) {
        printf("  Activation finished, but the script is not marked as active.\n");
        return false;
    }

    print_scheduled_time_line("Scheduled startup", &after_startup);
    print_scheduled_time_line("Scheduled shutdown", &after_shutdown);
    return true;
}


static bool prompt_local_schedule_file(char *selected, size_t selected_size) {
    char local_files[LOCAL_FILE_LIST_MAX][MAX_FILENAME_LENGTH];
    int file_count = scan_local_schedule_files(local_files, LOCAL_FILE_LIST_MAX);

    if (file_count > 0) {
        printf("  Available schedule files in current directory:\n");
        for (int i = 0; i < file_count; i++) {
            printf("  [%d] %s\n", i + 1, local_files[i]);
        }
    } else {
        printf("  No .wpi/.act/.skd files were found in current directory.\n");
    }

    printf("  Input a filename, or choose a number from the list above: ");
    char input[PATH_INPUT_MAX_LENGTH];
    if (read_line(input, sizeof(input)) == NULL || input[0] == '\0') {
        printf("\n");
        return false;
    }

    int selected_index = 0;
    if (is_valid_integer_in_range(input, 1, file_count, &selected_index)) {
        snprintf(selected, selected_size, "%s", local_files[selected_index - 1]);
        return true;
    }

    snprintf(selected, selected_size, "%s", input);
    return true;
}


static bool choose_remote_file_from_list(const char *title, uint8_t dir, bool hide_in_use_script,
                                         char *selected, size_t selected_size) {
    char files[REMOTE_FILE_LIST_MAX][MAX_FILENAME_LENGTH];
    int file_count = 0;
    if (!get_remote_file_list(dir, hide_in_use_script, files, REMOTE_FILE_LIST_MAX, &file_count)) {
        printf("  Failed to get file list from Witty Pi.\n");
        return false;
    }
    if (file_count <= 0) {
        printf("  No files are available for %s.\n", title);
        return false;
    }

    printf("  Available files for %s:\n", title);
    for (int i = 0; i < file_count; i++) {
        printf("  [%d] %s\n", i + 1, files[i]);
    }

    int selected_index = 0;
    if (!request_input_number("Please choose a file: ", 1, file_count, &selected_index, 2)) {
        return false;
    }

    snprintf(selected, selected_size, "%s", files[selected_index - 1]);
    return true;
}


static void choose_schedule_script_legacy(void);
static void manage_schedule_scripts(void);
static void download_log_file(void);


/**
 * Display the information bar
 */
void do_info_bar(void) {
    model = get_wittypi_model();
    if (model == MODEL_UNKNOWN) {
        printf("Can not detect Witty Pi, exiting...\n");
		exit(0);
    }

    printf("--------------------------------------------------------------------------------\n");
    int fw_major = 0;
    int fw_minor = 0;
    bool has_fw_version = get_firmware_version(&fw_major, &fw_minor);
    if (has_fw_version) {
        printf("  Model: %s (firmware v%d.%d)\n", wittypi_models[model], fw_major, fw_minor);
    } else {
        printf("  Model: %s\n", wittypi_models[model]);
    }

    float celsius = get_temperature();
    float fahrenheit = celsius_to_fahrenheit(celsius);
	printf("  Temperature: %.3f°C / %.3f°F\n", celsius, fahrenheit);
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
static int list_or_select_file_legacy(const char* input, int select, char* output, int output_len) {
    const char* start = input + 1;
    const char* current = start;
    int file_count = 0;

    while (*current && *current != '>') {
        const char* filename_start = current;

        while (*current && *current != '|' && *current != '>') {
            current++;
        }

        if (*current != '>' && strncmp(filename_start, IN_USE_SCRIPT_NAME, strlen(IN_USE_SCRIPT_NAME)) != 0) {
            file_count++;
            int len = (int)(current - filename_start);
            if (select <= 0) {
                printf("  [%d] %.*s\n", file_count, len, filename_start);
            } else if (select == file_count && output && output_len) {
                snprintf(output, (size_t)output_len, "%.*s", len, filename_start);
                return select;
            }
        }

        if (*current == '|') {
            current++;
        }
    }
    return file_count;
}


static bool pack_filename_legacy(char* filename, char* output) {
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


static void choose_schedule_script_legacy(void) {
    printf("--------------------------------------------------------------------------------\n");
    printf("  Choose schedule scripts\n");
    printf("--------------------------------------------------------------------------------\n");

    i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE);
    if (!run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_LIST_FILES, "list files")) {
        return;
    }

    char buf[DOWNLOAD_BUFFER_SIZE];
    int len = i2c_read_stream_util(-1, I2C_ADMIN_DOWNLOAD, (uint8_t *)buf, DOWNLOAD_BUFFER_SIZE - 1, '>');
    if (len <= 0) {
        printf("  Failed to read schedule list.\n");
        return;
    }
    buf[len] = '\0';

    printf("  Available schedule scripts on disk:\n");
    int file_count = list_or_select_file_legacy(buf, -1, NULL, -1);
    int select = 0;
    if (request_input_number("Please choose a schedule script: ", 1, file_count, &select, 2)) {
        list_or_select_file_legacy(buf, select, buf, DOWNLOAD_BUFFER_SIZE);
        printf("  You have chosen %s\n", buf);
        printf("  Please wait while processing...");
        fflush(stdout);

        pack_filename_legacy(buf, buf);
        i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE);
        i2c_write_stream_util(-1, I2C_ADMIN_UPLOAD, (uint8_t *)buf, DOWNLOAD_BUFFER_SIZE, '>');
        if (!run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT, "choose script")) {
            return;
        }

        sleep(1);
        int detected_model = MODEL_UNKNOWN;
        while (detected_model == MODEL_UNKNOWN) {
            sleep(1);
            detected_model = get_wittypi_model();
        }
        printf("done :)\n");
    }
}


static void upload_schedule_script(void) {
    char filename[PATH_INPUT_MAX_LENGTH];
    if (!prompt_local_schedule_file(filename, sizeof(filename))) {
        return;
    }

    if (upload_schedule_file(filename)) {
        printf("  Uploaded %s to Witty Pi.\n", filename);
    }
}


static void activate_schedule_script(void) {
    char filename[MAX_FILENAME_LENGTH];
    if (!choose_remote_file_from_list("activation", DIRECTORY_SCHEDULE, true, filename, sizeof(filename))) {
        return;
    }

    printf("  Activating %s\n", filename);
    activate_schedule_script_modern(filename);
}


static void download_schedule_script(void) {
    char filename[MAX_FILENAME_LENGTH];
    if (!choose_remote_file_from_list("download", DIRECTORY_SCHEDULE, false, filename, sizeof(filename))) {
        return;
    }

    if (access(filename, F_OK) == 0 && !user_confirm("The file already exists in current directory. Overwrite it?", 2)) {
        printf("  Task is cancelled.\n");
        return;
    }

    if (download_remote_file(DIRECTORY_SCHEDULE, filename, filename)) {
        printf("  Downloaded %s to current directory.\n", filename);
    } else {
        printf("  Failed to download %s.\n", filename);
    }
}


static void delete_schedule_script(void) {
    char filename[MAX_FILENAME_LENGTH];
    if (!choose_remote_file_from_list("deletion", DIRECTORY_SCHEDULE, false, filename, sizeof(filename))) {
        return;
    }

    char message[PATH_INPUT_MAX_LENGTH + 64];
    snprintf(message, sizeof(message), "Delete %s from Witty Pi?", filename);
    if (!user_confirm(message, 2)) {
        printf("  Task is cancelled.\n");
        return;
    }

    uint8_t packet[MAX_FILENAME_LENGTH + 8];
    size_t packet_len = 0;
    if (!pack_filename_hex(filename, packet, sizeof(packet), &packet_len)) {
        printf("  Failed to build delete packet.\n");
        return;
    }
    if (!i2c_set(-1, I2C_ADMIN_DIR, DIRECTORY_SCHEDULE) || !send_bytes_to_admin_upload(packet, packet_len)) {
        printf("  Failed to send delete request.\n");
        return;
    }

    uint8_t status = 0xFF;
    if (!run_admin_command_wait(I2C_ADMIN_PWD_CMD_FILE_DELETE, &status) || status != ADMIN_STATUS_OK) {
        printf("  Delete failed: %s\n", admin_status_text(status));
        return;
    }
    printf("  Deleted %s from Witty Pi.\n", filename);
}


static void manage_schedule_scripts(void) {
    if (!firmware_supports_file_management()) {
        print_requires_new_firmware_message();
        return;
    }

    printf("--------------------------------------------------------------------------------\n");
    printf("  Manage schedule scripts...\n");
    printf("--------------------------------------------------------------------------------\n");
    printf("  [1] Upload schedule script...\n");
    printf("  [2] Activate schedule script...\n");
    printf("  [3] Download schedule script...\n");
    printf("  [4] Delete schedule script...\n");
    printf("  [5] Return to main menu\n");

    int option_count = 5;
    printf("  Please input 1~%d: ", option_count);
    int value;
    bool valid;
    if (!input_number(1, option_count, &value, &valid, 2)) {
        printf("\n");
        usleep(100000);
        return;
    }
    if (!valid) {
        printf("\n");
        usleep(1000000);
        manage_schedule_scripts();
        return;
    }

    switch (value) {
        case 1:
            upload_schedule_script();
            break;
        case 2:
            activate_schedule_script();
            break;
        case 3:
            download_schedule_script();
            break;
        case 4:
            delete_schedule_script();
            break;
        case 5:
            return;
    }
    manage_schedule_scripts();
}


static void download_log_file(void) {
    if (!firmware_supports_file_management()) {
        print_requires_new_firmware_message();
        return;
    }

    const char *filename = "WittyPi5.log";
    if (!rename_existing_file_with_mtime(filename)) {
        printf("  Failed to preserve the existing %s in current directory.\n", filename);
        return;
    }

    if (download_remote_file(DIRECTORY_LOG, filename, filename)) {
        printf("  Downloaded %s to current directory.\n", filename);
    } else {
        printf("  Failed to download %s.\n", filename);
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
    printf("--------------------------------------------------------------------------------\n");
    printf("  Other Settings...\n");
    printf("--------------------------------------------------------------------------------\n");

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
    int psp = i2c_get(i2c_dev, I2C_CONF_PS_PRIORITY);
    if (psp == 0) {
        printf("  [10] Power source priority [V-USB first]\n");
    } else if (psp == 1) {
        printf("  [10] Power source priority [V-IN first]\n");
    } else if (psp < 0) {
        printf("  [10] Power source priority [Read error]\n");
    } else {
        printf("  [10] Power source priority [Invalid: %d]\n", psp);
    }

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

    // [13] VIN hot standby
    bool vin_hot_standby_supported = firmware_supports_vin_hot_standby();
    if (vin_hot_standby_supported) {
        int vhs = i2c_get(i2c_dev, I2C_CONF_VIN_HOT_STANDBY);
        if (vhs < 0) {
            printf("  [13] VIN hot standby when V-USB has priority [Read error]\n");
        } else {
            printf("  [13] VIN hot standby when V-USB has priority [%s]\n",
                   vhs ? "Enabled" : "Disabled");
        }
    } else {
        printf("  [13] VIN hot standby when V-USB has priority [N/A]\n");
    }
	
	// [14] Return to main menu
    printf("  [14] Return to main menu\n");
    
    close_i2c_device(i2c_dev);
    
    int optionCount = 14;
    
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
        case 13:// VIN hot standby
            if (!vin_hot_standby_supported) {
                printf("  VIN hot standby requires firmware v%d.%d or later.\n",
                       FW_VIN_HOT_STANDBY_REQUIRED_MAJOR,
                       FW_VIN_HOT_STANDBY_REQUIRED_MINOR);
                printf("  Please update the Witty Pi 5 firmware first.\n");
                sleep(2);
                break;
            }
            if (request_input_number("Keep VIN DC/DC enabled while V-USB has priority? (0=No, 1=Yes): ", 0, 1, &input, 2)) {
                if (i2c_set(-1, I2C_CONF_VIN_HOT_STANDBY, input)) {
                    printf("  VIN hot standby is now %s!\n", input ? "enabled" : "disabled");
                } else {
                    printf("  Failed to update VIN hot standby.\n");
                }
            } else {
                other_settings();
            }
            break;
		case 14:// Return to main menu
            return;
        default:
            break;
    }
}


/**
 * Display and process menu for resetting data
 */
void reset_data(void) {
    printf("--------------------------------------------------------------------------------\n");
    printf("  Reset data...\n");
    printf("--------------------------------------------------------------------------------\n");
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
			if (clear_startup_time()) {
                printf("  Scheduled startup time is cleared!\n");
            } else {
                printf("  Failed to clear startup time!\n");
            }
            return;
        case 2:	// Clear scheduled shutdown time
			if (clear_shutdown_time()) {
                printf("  Scheduled shutdown time is cleared!\n");
            } else {
                printf("  Failed to clear shutdown time!\n");
            }
            return;
		case 3:	// Stop using schedule script
            if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_PURGE_SCRIPT, "stop using schedule script")) {
                printf("  Schedule script is not being used now!\n");
            }
			return;
		case 4:	// Clear low-voltage threshold
			if (i2c_set(-1, I2C_CONF_LOW_VOLTAGE, 0)) {
                printf("  Low-voltage threshold is cleared!\n");
            } else {
                printf("  Failed to clear low-voltage threshold!\n");
            }
            return;
		case 5:	// Clear recovery-voltage threshold
			if (i2c_set(-1, I2C_CONF_RECOVERY_VOLTAGE, 0)) {
                printf("  Recovery-voltage threshold is cleared!\n");
            } else {
                printf("  Failed to clear recovery-voltage threshold!\n");
            }
            return;
		case 6: // Clear over-temperature action
			if (i2c_set(-1, I2C_CONF_OVER_TEMP_ACTION, 0)) {
                printf("  Over-temperature action is cleared!\n");
            } else {
                printf("  Failed to clear over-temperature action!\n");
            }
            return;
		case 7: // Clear below-temperature action
			if (i2c_set(-1, I2C_CONF_BELOW_TEMP_ACTION, 0)) {
                printf("  Below-temperature action is cleared!\n");
            } else {
                printf("  Failed to clear below-temperature action!\n");
            }
            return;
        case 8: // Reset all configuration values
            if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_RESET_CONF, "reset all configurations")) {
                printf("  All configuration values are reset!\n");
            }
            return;
		case 9: // Perform all actions above
			if (!clear_startup_time()) {
                printf("  Failed to clear startup time!\n");
                return;
            }
            usleep(100000);
			if (!clear_shutdown_time()) {
                printf("  Failed to clear shutdown time!\n");
                return;
            }
            usleep(100000);
            if (!run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_PURGE_SCRIPT, "stop using schedule script")) {
                return;
            }
            usleep(100000);
			int i2c_dev = open_i2c_device();
			if (i2c_dev >= 0) {
				if (!i2c_set(i2c_dev, I2C_CONF_LOW_VOLTAGE, 0)) {
                    printf("  Failed to clear low-voltage threshold!\n");
                }
				if (!i2c_set(i2c_dev, I2C_CONF_RECOVERY_VOLTAGE, 0)) {
                    printf("  Failed to clear recovery-voltage threshold!\n");
                }
				if (!i2c_set(i2c_dev, I2C_CONF_OVER_TEMP_ACTION, 0)) {
                    printf("  Failed to clear over-temperature action!\n");
                }
				if (!i2c_set(i2c_dev, I2C_CONF_BELOW_TEMP_ACTION, 0)) {
                    printf("  Failed to clear below-temperature action!\n");
                }
				close_i2c_device(i2c_dev);
			} else {
                printf("  Can not access I2C device!\n");
                return;
            }
            usleep(100000);
            if (!run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_RESET_CONF, "reset all configurations")) {
                return;
            }
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
    printf("--------------------------------------------------------------------------------\n");
    printf("  Administrate...\n");
    printf("--------------------------------------------------------------------------------\n");

    bool file_admin_supported = firmware_supports_file_management();

    printf("  [1] Print product information in log\n");
    printf("  [2] Format Witty Pi disk\n");
    printf("  [3] Reset RTC\n");
    printf("  [4] Turn on/off ID EEPROM write protection\n");
    printf("  [5] Synchronize configuration to file\n");
    printf("  [6] Save log to file\n");
    if (file_admin_supported) {
        printf("  [7] Download log file...\n");
        printf("  [8] Load and generate schedule scripts\n");
        printf("  [9] Return to main menu\n");
    } else {
        printf("  [7] Load and generate schedule scripts\n");
        printf("  [8] Return to main menu\n");
    }

    int optionCount = file_admin_supported ? 9 : 8;
    printf("  Please input 1~%d: ", optionCount);
    int value;
    bool valid;
    if (!input_number(1, optionCount, &value, &valid, 2)) {
        printf("\n");
        usleep(100000);
        return;
    }

    if (!valid) {
        printf("\n");
        usleep(1000000);
        administrate();
        return;
    }

    switch (value) {
        case 1:
            if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_PRINT_PRODUCT_INFO, "print product information")) {
                printf("  Product information is printed in log!\n\n");
            }
            break;
        case 2:
            if (user_confirm("All data on Witty Pi disk will be erased! Are you sure?", 2)) {
                if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_FORMAT_DISK, "format Witty Pi disk")) {
                    printf("  Witty Pi disk is formatted!\n\n");
                }
            } else {
                printf("  Task is cancelled.\n\n");
            }
            break;
        case 3:
            if (user_confirm("Do you want to reset the RTC?", 2)) {
                if(run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_RESET_RTC, "reset RTC")) {
                    printf("  RTC is reset!\n\n");
                }
            } else {
                printf("  Task is cancelled.\n\n");
            }
            break;
        case 4: {
            printf("  How to set the ID EEPROM write protection? (1=ON, 0=OFF): ");
            int input;
            bool input_valid;
            if (!input_number(0, 1, &input, &input_valid, 2) || !input_valid) {
                usleep(100000);
                printf("\n");
                break;
            }
            if (input) {
                if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_ENABLE_ID_EEPROM_WP, "turn on ID EEPROM write protection")) {
                    printf("  ID EEPROM write protection is ON.\n\n");
                }
            } else {
                if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_DISABLE_ID_EEPROM_WP, "turn off ID EEPROM write protection")) {
                    printf("  ID EEPROM write protection is OFF.\n\n");
                }
            }
            break;
        }
        case 5:
            if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_SYNC_CONF, "synchronize configuration with file")) {
                printf("  Configuration is synchronized to file on Witty Pi.\n\n");
            }
            break;
        case 6:
            if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_SAVE_LOG, "save log to file")) {
                printf("  Log is saved to file on Witty Pi.\n\n");
            }
            break;
        case 7:
            if (file_admin_supported) {
                download_log_file();
            } else {
                if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_LOAD_SCRIPT, "load schedule script")) {
                    printf("  Load schedule.wpi and generate .act and .skd files.\n\n");
                }
            }
            break;
        case 8:
            if (file_admin_supported) {
                if (run_admin_command_expect_ok(I2C_ADMIN_PWD_CMD_LOAD_SCRIPT, "load schedule script")) {
                    printf("  Load schedule.wpi and generate .act and .skd files.\n\n");
                }
            } else {
                return;
            }
            break;
        case 9:
            return;
    }
    administrate();
}


/**
 * Display and process the main menu
 */
void do_main_menu(void) {
    bool file_admin_supported = firmware_supports_file_management();

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
    if (file_admin_supported) {
        printf("  6. Manage schedule scripts...%s\n", is_script_in_use() ? " (in use)" : "");
    } else {
        printf("  6. Choose schedule script%s\n", is_script_in_use() ? " (in use)" : "");
    }
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
        return; // Invalid input, do nothing
    }

    if (!valid) {
        usleep(1000000);
    } else {
        switch (value) {
            case 1:     // Write system time to RTC
                if (system_to_rtc()) {
                    printf("System -> RTC OK\n");
                } else {
                    printf("Write failed\n");
                }
                break;
            case 2:     // Write RTC time to system
                if (rtc_to_system()) {
                    printf("RTC -> System OK\n");
                } else {
                    printf("Write failed\n");
                }
                break;
            case 3:     // Synchronize with network time
                if (network_to_system_and_rtc()) {
                    printf("Network -> System -> RTC OK\n");
                } else {
                    printf("Synchronization failed\n");
                }
                break;
            case 4:     // Schedule next shutdown
                schedule_shutdown();
                break;
            case 5:     // Schedule next startup
                schedule_startup();
                break;
            case 6:     // Manage schedule scripts.../Choose schedule script
                if (file_admin_supported) {
                    manage_schedule_scripts();
                } else {
                    choose_schedule_script_legacy();
                }
                break;
            case 7:     // Set low voltage threshold
                configure_low_voltage_threshold();
                break;
            case 8:     // Set recovery voltage threshold
                configure_recovery_voltage_threshold();
                break;
            case 9:     // Set over temperature action
                configure_over_temperature_action();
                break;
            case 10:    // Set below temperature action
                configure_below_temperature_action();
                break;
            case 11:    // Other settings...
                other_settings();
                break;
            case 12:    // Reset data...
                reset_data();
                break;
            case 13:    // Administrate...
                administrate();
                break;
            case 14:    // Exit
                handle_signal(2);
                break;
        }
    }
}


/**
 * Main function
 */
int main(int argc, char *argv[]) {

    if (!acquire_wp5_app_lock()) {
        printf("Another wp5 instance is already running. Please close it first.\n");
        return 1;
    }

    atexit(release_wp5_app_lock);
    
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