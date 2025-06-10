#define _XOPEN_SOURCE
#define _GNU_SOURCE

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <errno.h>

#include "wp5lib.h"


#define ACQUIRE_I2C_LOCK_MAX_ATTEMPTS   5
#define ACQUIRE_I2C_LOCK_INTERVAL_US    200000

#define I2C_POST_WRITE_SETTLE_DELAY_US  1000

#define I2C_WRITE_MAX_ATTEMPTS			10
#define I2C_WRITE_VALIDATE_DELAY_US     100

#define I2C_READ_MAX_ATTEMPTS   		10
#define I2C_READ_VALIDATE_COUNT   		2


static LogMode log_mode = LOG_WITH_TIME;


const char *wittypi_models[] = {
    "Unknown",
    "Witty Pi 5",
    "Witty Pi 5 Mini",
    "Witty Pi 5 L3V7",
};
const int wittypi_models_count = sizeof(wittypi_models) / sizeof(wittypi_models[0]);

const char *action_reasons[] = {
    "Unknown",
    "Scheduled Startup",
    "Scheduled Shutdown",
	"Button Click",
	"Vin < Vlow",
	"Vin > Vrec",
	"Over Temperature",
	"Below Temperature",
	"Power Newly Connected",
	"Reboot",
	"Missed Heartbeat",
	"Shutdown Externally",
	"Reboot Externally",
};
const int action_reasons_count = sizeof(action_reasons) / sizeof(action_reasons[0]);


/**
 * Set log mode
 *
 * @param mode The log mode to be used
 */
void set_log_mode(LogMode mode) {
    log_mode = mode;
}


/**
 * Print log function that accepts same arguments as printf
 *
 * @param format Format string in printf style
 * @param ... Variable arguments for the format string
 * @return Number of characters printed (excluding null byte)
 */
int print_log(const char *format, ...) {
    if (log_mode == LOG_WITH_TIME) {
        time_t now;
        time(&now);
        struct tm *local_time = localtime(&now);

        char timestamp[20]; // YYYY-MM-DD HH:MM:SS
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", local_time);

        int timestamp_chars = printf("[%s] ", timestamp);

        va_list args;
        va_start(args, format);
        int printed_chars = vprintf(format, args);
        va_end(args);

        const char *last_char = format + strlen(format) - 1;
        if (*last_char != '\n') {
            printf("\n");
            printed_chars += 1;
        }

        fflush(stdout);
        return timestamp_chars + printed_chars;
    } else if (log_mode == LOG_WITHOUT_TIME) {
        va_list args;
        int result;
        va_start(args, format);
        result = vprintf(format, args);
        va_end(args);
        return result;
    } else {
        return 0;
    }
}


/**
 * Print system information
 */
void print_sys_info(void) {
    struct utsname info;
    if (uname(&info) == 0) {
        print_log("System: %s, Kernel: %s, Architecture: %s\n", info.sysname, info.release, info.machine);
    }
}


/**
 * Print Raspberry Pi information
 */
void print_pi_info(void) {
    FILE *fp;
    char model[256];
    if ((fp = fopen("/proc/device-tree/model", "r")) && 
        fread(model, 1, sizeof(model) - 1, fp)) {
        model[255] = '\0';
        print_log("Running on: %s\n", model);
        fclose(fp);
    } else {
        print_log("Running on: Unknown Pi Model\n");
        if (fp) fclose(fp);
    }
}


/**
 * Calculates the CRC-8 checksum for a data buffer
 *
 * @param data Pointer to the data buffer
 * @param len Length of the data buffer (in bytes)
 * @return uint8_t The calculated CRC-8 checksum (single byte)
 */
uint8_t calculate_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        // XOR the current data byte with the current CRC value
        crc ^= data[i];
        // Perform 8 shifts and XOR operations (simulate polynomial division)
        for (int j = 0; j < 8; j++) {
            // Check the most significant bit (MSB) of the current CRC value
            if ((crc & 0x80) != 0) {
                // If MSB is 1, left shift CRC by 1 and XOR with the polynomial
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                // If MSB is 0, just left shift CRC by 1
                crc <<= 1;
            }
        }
    }
    return crc;
}


// Acquire I2C lock
int lock_file() {
    int lock_fd = -1;
    int attempts = 0;
    while (lock_fd < 0) {
        attempts ++;
        mode_t old_umask = umask(0);
        lock_fd = open(I2C_LOCK, O_CREAT | O_RDWR, 0666);
        umask(old_umask);
        if (lock_fd < 0) {
            print_log("Failed to open lock file %s\n", I2C_LOCK);
            return -1;
        }
        if (flock(lock_fd, LOCK_EX) < 0) {
            print_log("Failed to acquire I2C lock\n");
            close(lock_fd);
            if (attempts >= ACQUIRE_I2C_LOCK_MAX_ATTEMPTS) {
                return -1;
            }
            usleep(ACQUIRE_I2C_LOCK_INTERVAL_US);
        }
    }
    return lock_fd;
}


// Release I2C lock
void unlock_file(int lock_fd) {
    flock(lock_fd, LOCK_UN);
    close(lock_fd);
}


/**
 * Open I2C device
 *
 * @return The handler of the device if open succesfully, -1 otherwise
 */
int open_i2c_device(void) {
    int i2c_dev = open(I2C_DEVICE, O_RDWR);
    if (i2c_dev < 0) {
        print_log("Failed to open I2C device.\n");
        return -1;
    }

    if (ioctl(i2c_dev, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
        print_log("Failed setting I2C slave device address.\n");
        return -1;
    }
    return i2c_dev;
}


/**
 * Read value from I2C register, with or without validation
 * When reading value that may change quickly, validation should not be used
 *
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param validate Whether to validate the value
 * @return The value if read succesfully, -1 otherwise
 */
int i2c_get_impl(int i2c_dev, uint8_t index, bool validate) {
    bool need_to_close = false;
    if (i2c_dev < 0) {
        i2c_dev = open_i2c_device();
        if (i2c_dev < 0) {
            print_log("i2c_get: can not open I2C device.\n");
            return -1;
        }
        need_to_close = true;
    }

    int value = -1;
    int attempts = 0;
    int same_value_count = 0;
    uint8_t last_read_value = 0;

    while (attempts < I2C_READ_MAX_ATTEMPTS && same_value_count < (validate ? I2C_READ_VALIDATE_COUNT : 1)) {
        attempts++;

        int lock_fd = lock_file();
        if (lock_fd < 0) {
            print_log("i2c_get: failed to lock I2C device.\n");
            usleep(1000);
            continue;
        }

        uint8_t read_buffer[1];
        uint8_t read_addr_buffer[1];
        read_addr_buffer[0] = index;

        struct i2c_msg msgs[2];
        struct i2c_rdwr_ioctl_data msgs_data;

        msgs[0].addr = I2C_SLAVE_ADDR;
        msgs[0].flags = 0;
        msgs[0].len = 1;
        msgs[0].buf = read_addr_buffer;

        msgs[1].addr = I2C_SLAVE_ADDR;
        msgs[1].flags = I2C_M_RD;
        msgs[1].len = 1;
        msgs[1].buf = read_buffer;

        msgs_data.msgs = msgs;
        msgs_data.nmsgs = 2;

        if (ioctl(i2c_dev, I2C_RDWR, &msgs_data) < 0) {
            print_log("i2c_get: read transaction failed for Reg%d on attempt %d: %s\n", index, attempts, strerror(errno));
            unlock_file(lock_fd);
            usleep(1000);
            if (validate) {
                continue;
            }
        }

        unlock_file(lock_fd);

        uint8_t current_read_value = read_buffer[0];

        if (validate) {
            if (attempts == 1) {
                 last_read_value = current_read_value;
                 same_value_count = 1;
            } else {
                if (current_read_value == last_read_value) {
                    same_value_count++;
                } else {
                    print_log("i2c_get: Reg%d value changed from 0x%02x to 0x%02x on attempt %d.\n", index, last_read_value, current_read_value, attempts);
                    last_read_value = current_read_value;
                    same_value_count = 1;
                }
            }
        } else {
            value = current_read_value;
            break;
        }

        if (validate && same_value_count >= I2C_READ_VALIDATE_COUNT) {
             value = current_read_value;
             break;
        }
    }
    if (attempts >= I2C_READ_MAX_ATTEMPTS && (validate && same_value_count < I2C_READ_VALIDATE_COUNT)) {
        print_log("i2c_get: Failed to get stable reading for Reg%d after %d attempts.\n", index, attempts);
        value = -1;
    }
    if (need_to_close) {
        close(i2c_dev);
    }
    return value;
}



/**
 * Read value from I2C register with validation
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @return The value if read succesfully, -1 otherwise
 */
int i2c_get(int i2c_dev, uint8_t index) {
	return i2c_get_impl(i2c_dev, index, true);
}


/**
 * Read data from specific I2C register until expected value is read
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the stream register
 * @param buf The pointer to the buffer
 * @param size The size of buffer
 * @param expected The byte that would stop the reading
 * @return The length of data read, -1 if error
 */
int i2c_read_stream_util(int i2c_dev, uint8_t index, uint8_t * buf, int size, uint8_t expected) {
    bool need_to_close = false;
    if (i2c_dev < 0) {
        i2c_dev = open_i2c_device();
        if (i2c_dev < 0) {
            print_log("i2c_read_stream_util: can not open I2C device.\n");
            return -1;
        }
        need_to_close = true;
    }
    int i, len;
    for (i = 0; i < size; i ++) {
        buf[i] = (uint8_t)i2c_get_impl(i2c_dev, index, false);
        if (buf[i] == expected) {
            break;
        }
    }
    len = i + 1;
    if (need_to_close) {
        close(i2c_dev);
    }
    return len;
}


/**
 * Write value to I2C register, with or without validation
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param value The value of the register
 * @param validate Whether to validate the value
 * @return true if successfully written, false otherwise
 */
bool i2c_set_impl(int i2c_dev, uint8_t index, uint8_t value, bool validate) {
    bool need_to_close = false;
    if (i2c_dev < 0) {
        i2c_dev = open_i2c_device();
        if (i2c_dev < 0) {
            print_log("i2c_set: can not open I2C device.\n");
            return false;
        }
        need_to_close = true;
    }
    bool success = true;
    int attempts = 0;
    while (true) {
        attempts++;
        if (attempts > I2C_WRITE_MAX_ATTEMPTS) {
            print_log("i2c_set: too many retries, give up.\n");
            success = false;
            break;
        }

        int lock_fd = lock_file();
        if (lock_fd < 0) {
            print_log("i2c_set: failed to lock I2C device.\n");
            success = false;
            usleep(1000);
            continue;
        }

        if (!validate) {    // Write without validation
            uint8_t buffer[2];
            buffer[0] = index;
            buffer[1] = value;

            struct i2c_msg msg;
            struct i2c_rdwr_ioctl_data msgs_data;

            msg.addr = I2C_SLAVE_ADDR;
            msg.flags = 0;
            msg.len = 2;
            msg.buf = buffer;

            msgs_data.msgs = &msg;
            msgs_data.nmsgs = 1;

            if (ioctl(i2c_dev, I2C_RDWR, &msgs_data) < 0) {
                print_log("i2c_set: simple write failed.\n");
                success = false;
            }
            unlock_file(lock_fd);
            break;
        } else {            // Write and validate
            // Write the value with 1 message
            uint8_t write_buffer[2];
            write_buffer[0] = index;
            write_buffer[1] = value;
        
            struct i2c_msg write_msg;
            struct i2c_rdwr_ioctl_data write_msgs_data;
        
            write_msg.addr = I2C_SLAVE_ADDR;
            write_msg.flags = 0;
            write_msg.len = 2;
            write_msg.buf = write_buffer;
        
            write_msgs_data.msgs = &write_msg;
            write_msgs_data.nmsgs = 1;
        
            if (ioctl(i2c_dev, I2C_RDWR, &write_msgs_data) < 0) {
                print_log("i2c_set: Error writing I2C register.\n");
                success = false;
                unlock_file(lock_fd);
                continue;
            }

            // Some delay
            usleep(I2C_WRITE_VALIDATE_DELAY_US);
        
            // Read back the value with 2 messages (writing index and reading value)
            uint8_t read_buffer[1];
            uint8_t read_addr_buffer[1];
            read_addr_buffer[0] = index;
        
            struct i2c_msg read_msgs[2];
            struct i2c_rdwr_ioctl_data read_msgs_data;
        
            read_msgs[0].addr = I2C_SLAVE_ADDR;
            read_msgs[0].flags = 0;
            read_msgs[0].len = 1;
            read_msgs[0].buf = read_addr_buffer;
        
            read_msgs[1].addr = I2C_SLAVE_ADDR;
            read_msgs[1].flags = I2C_M_RD;
            read_msgs[1].len = 1;
            read_msgs[1].buf = read_buffer;
        
            read_msgs_data.msgs = read_msgs;
            read_msgs_data.nmsgs = 2;
        
            if (ioctl(i2c_dev, I2C_RDWR, &read_msgs_data) < 0) {
                print_log("i2c_set: Error reading I2C register for validation.\n");
                success = false;
                unlock_file(lock_fd);
                continue;
            }
            
            unlock_file(lock_fd);

            // Validate the value
            if (read_buffer[0] == value) {
                success = true;
                break;
            } else {
                print_log("i2c_set: set Reg%d to 0x%02x but read back 0x%02x. Retrying...\n", index, value, read_buffer[0]);
            }
        }
    }
    if (need_to_close) {
        close(i2c_dev);
    }
    return success;
}


/**
 * Write value to I2C register with validation
 *
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param value The value of the register
 * @return true if successfully written, false otherwise
 */
bool i2c_set(int i2c_dev, uint8_t index, uint8_t value) {
	return i2c_set_impl(i2c_dev, index, value, true);
}


/**
 * Write data to specific I2C register until expected value appear
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the stream register
 * @param buf The pointer to the buffer
 * @param size The size of buffer
 * @param expected The byte that would stop the writing
 * @return The length of data wrote, -1 if error
 */
int i2c_write_stream_util(int i2c_dev, uint8_t index, uint8_t * buf, int size, uint8_t expected) {
    bool need_to_close = false;
    if (i2c_dev < 0) {
        i2c_dev = open_i2c_device();
        if (i2c_dev < 0) {
            print_log("i2c_write_stream_util: can not open I2C device.\n");
            return -1;
        }
        need_to_close = true;
    }
    int i, len;
    for (i = 0; i < size; i ++) {
        if (-1 == i2c_set_impl(i2c_dev, index, buf[i], false)) {
            i = -2;
            break;
        }
        if (buf[i] == expected) {
            break;
        }
    }
    len = i + 1;
    if (need_to_close) {
        close(i2c_dev);
    }
    return len;
}


/**
 * Close I2C device
 *
 * @param i2c_dev The I2C device handler
 */
void close_i2c_device(int i2c_dev) {
    if (i2c_dev >= 0) {
        close(i2c_dev);
    }
}


/**
 * Get Witty Pi model
 *
 * @return The model of Witty Pi
 */
int get_wittypi_model(void) {
    LogMode bk_mode = log_mode;
    log_mode = LOG_NONE;
    int dev = open_i2c_device();
    if (dev < 0) {
        log_mode = bk_mode;
        return MODEL_UNKNOWN;
    }
    int fw_id = -1;
    int attempts = 0;
    while(fw_id == -1) {
        fw_id = i2c_get_impl(dev, I2C_FW_ID, false);
        attempts ++;
        if (attempts >= 3) {
            log_mode = bk_mode;
			return MODEL_UNKNOWN;
		}
		if (fw_id == -1) {
		    usleep(100000);
	    }
    }
    close(dev);
    log_mode = bk_mode;
    switch (fw_id) {
        case FW_ID_WITTYPI_5:
            return MODEL_WITTYPI_5;
        case FW_ID_WITTYPI_5_MINI:
            return MODEL_WITTYPI_5_MINI;
        case FW_ID_WITTYPI_5_L3V7:
            return MODEL_WITTYPI_5_L3V7;
    }
    return MODEL_UNKNOWN;
}


/**
 * Get power mode
 *
 * @return 0 if powered via USB, 1 if powered via VIN, 255 if not powered, -1 if error
 */
int get_power_mode(void) {
    return i2c_get(-1, I2C_POWER_MODE);
}


/**
 * Get temperature
 * 
 * @return Temperature in Celsius degree, -1000.0 if error
 */
float get_temperature(void) {
	int dev = open_i2c_device();
    if (dev < 0) {
        return -1000.0f;
    }
	int msb = i2c_get_impl(dev, I2C_VREG_TMP112_TEMP_MSB, false);
    int lsb = i2c_get_impl(dev, I2C_VREG_TMP112_TEMP_LSB, false);
	if (msb < 0 || lsb < 0) {
		return -1000.0f;
	}
	int16_t raw = (msb << 4) | (lsb >> 4);
    if (raw & 0x800) {
        raw |= 0xF000;
    }
    return (float)((int32_t)raw * 625) / 10000.0f;
}


/**
 * Convert temperature from Celsius to Fahrenheit
 * 
 * @param celsius Temperature in Celsius degree
 * @return Temperature in Fahrenheit degree
 */
float celsius_to_fahrenheit(float celsius) {
    return celsius * 1.8f + 32.0f;
}


// Get 16-bit value from two I2C registers and return its thousandth value
float get_thousandth(uint8_t msb_index, uint8_t lsb_index) {
    int dev = open_i2c_device();
    if (dev < 0) {
        return -1.0f;
    }
    int msb = i2c_get_impl(dev, msb_index, false) & 0x7F;
    int lsb = i2c_get_impl(dev, lsb_index, false);
    close(dev);
    return (float)((msb << 8) | lsb) / 1000.0f;
}


/**
 * Get input voltage (Vin)
 *
 * @return Input voltage
 */
float get_vin(void) {
    return get_thousandth(I2C_VIN_MV_MSB, I2C_VIN_MV_LSB);
}


/**
 * Get USB-C voltage (Vusb)
 *
 * @return USB-C voltage
 */
float get_vusb(void) {
    return get_thousandth(I2C_VUSB_MV_MSB, I2C_VUSB_MV_LSB);
}


/**
 * Get output voltage (Vout)
 *
 * @return Output voltage
 */
float get_vout(void) {
    return get_thousandth(I2C_VOUT_MV_MSB, I2C_VOUT_MV_LSB);
}


/**
 * Get output current (Iout)
 *
 * @return Output current
 */
float get_iout(void) {
    return get_thousandth(I2C_IOUT_MA_MSB, I2C_IOUT_MA_LSB);
}


/**
 * Get local system time information
 * 
 * @param dt The DateTime object to save the result
 * @return true if succeed, otherwise false
 */
bool get_system_time(DateTime * dt) {
    if (dt == NULL) {
        return false;
    }

    time_t current_time;
    time(&current_time);
    struct tm *time_info = localtime(&current_time);

    if (time_info != NULL) {
        dt->year = time_info->tm_year + 1900;  // tm_year is years since 1900
        dt->month = time_info->tm_mon + 1;     // tm_mon ranges from 0-11
        dt->day = time_info->tm_mday;          // tm_mday ranges from 1-31
        dt->hour = time_info->tm_hour;         // tm_hour ranges from 0-23
        dt->min = time_info->tm_min;           // tm_min ranges from 0-59
        dt->sec = time_info->tm_sec;           // tm_sec ranges from 0-59
        dt->wday = time_info->tm_wday;         // tm_wday ranges from 0-6 (0 = Sunday)
		return true;
    }
	return false;
}


/**
 * Get RTC time information
 *
 * @param dt The DateTime object to save the result
 * @return true if succeed, otherwise false
 */
bool get_rtc_time(DateTime * dt) {
    if (dt == NULL) {
        return false;
    }

    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    int sec = i2c_get(i2c_dev, I2C_VREG_RX8025_SEC);
    int min = i2c_get(i2c_dev, I2C_VREG_RX8025_MIN);
    int hour = i2c_get(i2c_dev, I2C_VREG_RX8025_HOUR);
    int wday = i2c_get(i2c_dev, I2C_VREG_RX8025_WEEKDAY);
    int day = i2c_get(i2c_dev, I2C_VREG_RX8025_DAY);
    int month = i2c_get(i2c_dev, I2C_VREG_RX8025_MONTH);
    int year = i2c_get(i2c_dev, I2C_VREG_RX8025_YEAR);
    close_i2c_device(i2c_dev);

    if (sec < 0 || min < 0 || hour < 0 || wday < 0 ||
        day < 0 || month < 0 || year < 0) {
        return false;
    }

    dt->sec = bcd_to_dec(sec);
    dt->min = bcd_to_dec(min);
    dt->hour = bcd_to_dec(hour);
    dt->wday = bcd_to_dec(wday) & 0x07;
    dt->day = bcd_to_dec(day);
    dt->month = bcd_to_dec(month);
    dt->year = 2000 + bcd_to_dec(year);
	return true;
}


/**
 * Check if the time stored in DateTime is valid
 * 
 * @param dt The DateTime object to save the time
 * @return true if valid, otherwise false
 */
bool is_time_valid(DateTime * dt) {
    if (!dt) {
        return false;
    }
    return dt->sec < 60 && dt->min < 60 && dt->hour < 24 && dt->day > 0 && dt->day <= 31 && dt->month > 0 && dt->month <= 12 && dt->year > 2024;
}


/**
 * Write system time into RTC
 *
 * @return true if succeed, otherwise false
 */
bool system_to_rtc(void) {
	DateTime sys_dt;
	if (get_system_time(&sys_dt)) {
        int i2c_dev = open_i2c_device();
        if (i2c_dev < 0) {
            return false;
        }
		
        bool success = true;
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_SEC, dec_to_bcd(sys_dt.sec));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_MIN, dec_to_bcd(sys_dt.min));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_HOUR, dec_to_bcd(sys_dt.hour));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_WEEKDAY, BIT_VALUE(dec_to_bcd(sys_dt.wday)));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_DAY, dec_to_bcd(sys_dt.day));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_MONTH, dec_to_bcd(sys_dt.month));
        success &= i2c_set(i2c_dev, I2C_VREG_RX8025_YEAR, dec_to_bcd(sys_dt.year - 2000));

        close_i2c_device(i2c_dev);
        return success;
	}
	return false;
}


/**
 * Write RTC time into system
 *
 * @return true if succeed, otherwise false
 */
bool rtc_to_system(void) {
	DateTime rtc_dt;
	if (get_rtc_time(&rtc_dt)) {
        char date_cmd[64];
        snprintf(date_cmd, sizeof(date_cmd),
                "sudo date -s \"%04d-%02d-%02d %02d:%02d:%02d\"",
                rtc_dt.year, rtc_dt.month, rtc_dt.day,
                rtc_dt.hour, rtc_dt.min, rtc_dt.sec);
        int result = system(date_cmd);
        if (result == 0) {
            return true;
        }
	}
	return false;
}


/**
 * Write network time into system and RTC
 * 
 * @return true if succeed, otherwise false
 */
bool network_to_system_and_rtc(void) {
    int sockfd;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[4096] = {0};
    char http_request[] = "HEAD / HTTP/1.1\r\nHost: www.google.com\r\nConnection: close\r\n\r\n";
    char date_header[128] = {0};
    struct tm time_info = {0};
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return false;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    
    server = gethostbyname("www.google.com");
    if (server == NULL) {
        close(sockfd);
        return false;
    }
    
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(80);
    
    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sockfd);
        return false;
    }
    
    if (write(sockfd, http_request, strlen(http_request)) < 0) {
        close(sockfd);
        return false;
    }
    
    int n = read(sockfd, buffer, sizeof(buffer) - 1);
    close(sockfd);
    if (n <= 0) {
        return false;
    }
    buffer[n] = 0;
    
    // Find Date header in HTTP response
    char *date_ptr = strstr(buffer, "Date: ");
    if (!date_ptr) {
        return false;
    }
    
    int i = 0;
    date_ptr += 6;  // Skip "Date: "
    while (*date_ptr && *date_ptr != '\r' && i < sizeof(date_header) - 1) {
        date_header[i++] = *date_ptr++;
    }
    date_header[i] = 0;
    
    // Parse date string (format: "Day, DD Mon YYYY HH:MM:SS GMT")
    if (strptime(date_header, "%a, %d %b %Y %H:%M:%S GMT", &time_info) == NULL) {
        return false;
    }
    
    time_t utc_time;
    time_info.tm_isdst = 0;
    
    char *old_tz = getenv("TZ");
    char old_tz_buf[256] = {0};
    if (old_tz) {
        strncpy(old_tz_buf, old_tz, sizeof(old_tz_buf) - 1);
    }
    
    setenv("TZ", "UTC", 1);
    tzset();
    
    utc_time = mktime(&time_info);
    
    if (old_tz) {
        setenv("TZ", old_tz_buf, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    
    if (utc_time == (time_t)-1) {
        return false;
    }
    
    // Convert UTC time to local time
    struct tm *local_tm = localtime(&utc_time);
    
    char date_cmd[64];
    snprintf(date_cmd, sizeof(date_cmd),
            "sudo date -s \"%04d-%02d-%02d %02d:%02d:%02d\"",
            local_tm->tm_year + 1900, local_tm->tm_mon + 1, local_tm->tm_mday,
            local_tm->tm_hour, local_tm->tm_min, local_tm->tm_sec);
    int result = system(date_cmd);
    if (result != 0) {
        return false;
    }
    
    return system_to_rtc();
}


/**
 * Get scheduled startup time as individual components
 * 
 * @param date Pointer to store the day value (1-31)
 * @param hour Pointer to store the hour value (0-23)
 * @param minute Pointer to store the minute value (0-59)
 * @param second Pointer to store the second value (0-59)
 * @return true if succeed, false if fail or no valid startup has been scheduled
 */
bool get_startup_time(uint8_t * date, uint8_t * hour, uint8_t * minute, uint8_t * second) {
	if (!date || !hour || !minute || !second) {
		return false;
	}
	
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    
    int sec_val = i2c_get(i2c_dev, I2C_CONF_ALARM1_SECOND);
    int min_val = i2c_get(i2c_dev, I2C_CONF_ALARM1_MINUTE);
    int hour_val = i2c_get(i2c_dev, I2C_CONF_ALARM1_HOUR);
    int day_val = i2c_get(i2c_dev, I2C_CONF_ALARM1_DAY);
    
    close_i2c_device(i2c_dev);
    
    if (sec_val < 0 || min_val < 0 || hour_val < 0 || day_val < 0) {
        return false;
    }
    
    *second = bcd_to_dec((uint8_t)sec_val);
    *minute = bcd_to_dec((uint8_t)min_val);
    *hour = bcd_to_dec((uint8_t)hour_val);
    *date = bcd_to_dec((uint8_t)day_val);
    
    if (*second > 59 || *minute > 59 || *hour > 23 || *date == 0 || *date > 31) {
        return false;
    }
    return true;
}


/**
 * Get scheduled shutdown time as individual components
 * 
 * @param date Pointer to store the day value (1-31)
 * @param hour Pointer to store the hour value (0-23)
 * @param minute Pointer to store the minute value (0-59)
 * @param second Pointer to store the second value (0-59)
 * @return true if succeed, false if fail or no valid shutdown has been scheduled
 */
bool get_shutdown_time(uint8_t * date, uint8_t * hour, uint8_t * minute, uint8_t * second) {
	if (!date || !hour || !minute || !second) {
		return false;
	}
	
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    
    int sec_val = i2c_get(i2c_dev, I2C_CONF_ALARM2_SECOND);
    int min_val = i2c_get(i2c_dev, I2C_CONF_ALARM2_MINUTE);
    int hour_val = i2c_get(i2c_dev, I2C_CONF_ALARM2_HOUR);
    int day_val = i2c_get(i2c_dev, I2C_CONF_ALARM2_DAY);
    
    close_i2c_device(i2c_dev);
    
    if (sec_val < 0 || min_val < 0 || hour_val < 0 || day_val < 0) {
        return false;
    }
    
    *second = bcd_to_dec((uint8_t)sec_val);
    *minute = bcd_to_dec((uint8_t)min_val);
    *hour = bcd_to_dec((uint8_t)hour_val);
    *date = bcd_to_dec((uint8_t)day_val);
    
    if (*second > 59 || *minute > 59 || *hour > 23 || *date == 0 || *date > 31) {
        return false;
    }
    return true;
}


/**
 * Set scheduled startup time
 * 
 * @param date The date (1-31)
 * @param hour The hour (0-23)
 * @param minute The minute (0-59)
 * @param second The second (0-59)
 * @return true if succeed, false if fail
 */
bool set_startup_time(uint8_t date, uint8_t hour, uint8_t minute, uint8_t second) {
    if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    
    uint8_t bcd_second = dec_to_bcd(second);
    uint8_t bcd_minute = dec_to_bcd(minute);
    uint8_t bcd_hour = dec_to_bcd(hour);
    uint8_t bcd_date = dec_to_bcd(date);
    
    bool success = true;
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_SECOND, bcd_second);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_MINUTE, bcd_minute);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_HOUR, bcd_hour);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_DAY, bcd_date);
    
    close_i2c_device(i2c_dev);
    return success;
}


/**
 * Clear scheduled startup time
 * 
 * @return true if succeed, false if fail
 */
bool clear_startup_time() {
	int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
	bool success = true;
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_SECOND, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_MINUTE, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_HOUR, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM1_DAY, 0);
	close_i2c_device(i2c_dev);
    return success;	
}


/**
 * Set scheduled shutdown time
 * 
 * @param date The date (1-31)
 * @param hour The hour (0-23)
 * @param minute The minute (0-59)
 * @param second The second (0-59)
 * @return true if succeed, false if fail
 */
bool set_shutdown_time(uint8_t date, uint8_t hour, uint8_t minute, uint8_t second) {
    if (date < 1 || date > 31 || hour > 23 || minute > 59 || second > 59) {
        return false;
    }
    
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    
    uint8_t bcd_second = dec_to_bcd(second);
    uint8_t bcd_minute = dec_to_bcd(minute);
    uint8_t bcd_hour = dec_to_bcd(hour);
    uint8_t bcd_date = dec_to_bcd(date);
    
    bool success = true;
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_SECOND, bcd_second);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_MINUTE, bcd_minute);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_HOUR, bcd_hour);
    success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_DAY, bcd_date);
    
    close_i2c_device(i2c_dev);
    return success;
}


/**
 * Clear scheduled shutdown time
 * 
 * @return true if succeed, false if fail
 */
bool clear_shutdown_time() {
	int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
	bool success = true;
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_SECOND, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_MINUTE, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_HOUR, 0);
	success &= i2c_set(i2c_dev, I2C_CONF_ALARM2_DAY, 0);
	close_i2c_device(i2c_dev);
    return success;	
}


/**
 * Get low voltage threshold in Volt
 * 
 * @return The low voltage threshold in Volt, -1 if fail
 */
float get_low_voltage_threshold(void) {
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return -1;
    }
    int value = i2c_get(i2c_dev, I2C_CONF_LOW_VOLTAGE);
    close_i2c_device(i2c_dev);
    if (value <= 0) {
        return -1;
    }
    return value / 10.0f;
}


/**
 * Set low voltage threshold in Volt
 * 
 * @param threshold Low voltage threshold in Volt
 * @return true if succeed, false if fail
 */
bool set_low_voltage_threshold(float threshold) {
    if (threshold < 0) {
        return false;
    }
    uint8_t value = (uint8_t)(threshold * 10.0f);
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    bool result = i2c_set(i2c_dev, I2C_CONF_LOW_VOLTAGE, value);
    close_i2c_device(i2c_dev);
    return result;
}


/**
 * Get recovery voltage threshold in Volt
 * 
 * @return The recovery voltage threshold in Volt, -1 if fail
 */
float get_recovery_voltage_threshold(void) {
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return -1;
    }
    int value = i2c_get(i2c_dev, I2C_CONF_RECOVERY_VOLTAGE);
    close_i2c_device(i2c_dev);
    if (value <= 0) {
        return -1;
    }
    return value / 10.0f;
}


/**
 * Set recovery voltage threshold in Volt
 * 
 * @param threshold Recovery voltage threshold in Volt
 * @return true if succeed, false if fail
 */
bool set_recovery_voltage_threshold(float threshold) {
    if (threshold < 0) {
        return false;
    }
    uint8_t value = (uint8_t)(threshold * 10.0f);
    int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    bool result = i2c_set(i2c_dev, I2C_CONF_RECOVERY_VOLTAGE, value);
    close_i2c_device(i2c_dev);
    return result;
}


/**
 * Run administrative command
 * 
 * @param psw_cmd The 16 bit integer that stores password and command
 * @return true if succeed, false if fail
 */
bool run_admin_command(uint16_t psw_cmd) {
	int i2c_dev = open_i2c_device();
    if (i2c_dev < 0) {
        return false;
    }
    bool result = true;
	uint8_t psw = (psw_cmd >> 8);
	uint8_t cmd = (psw_cmd & 0xFF);
	result &= i2c_set(i2c_dev, I2C_ADMIN_PASSWORD, psw);
	result &= i2c_set_impl(i2c_dev, I2C_ADMIN_COMMAND, cmd, false);
    close_i2c_device(i2c_dev);
    return result;
}


/**
 * Check if schedule script is in use
 * 
 * @return true if is in use, otherwise false
 */
bool is_script_in_use(void) {
    int misc = i2c_get(-1, I2C_MISC);
    return misc & 0x01;
}


/**
 * Get the reason for recent startup
 * 
 * @return the reason code
 */
int get_startup_reason(void) {
    int reg = i2c_get(-1, I2C_ACTION_REASON);
    if (reg < 0) {
        return ACTION_REASON_UNKNOWN;
    }
    return ((reg & 0xF0) >> 4);
}


/**
 * Get the reason for recent shutdown
 * 
 * @return the reason code
 */
int get_shutdown_reason(void) {
    int reg = i2c_get(-1, I2C_ACTION_REASON);
    if (reg < 0) {
        return ACTION_REASON_UNKNOWN;
    }
    return (reg & 0x0F);
}
