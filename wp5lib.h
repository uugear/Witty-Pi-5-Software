#ifndef __WP5LIB_H
#define __WP5LIB_H

#include <stdbool.h>
#include <stdint.h>

#define STRINGIFY(x) #x
#define TO_STRING(x) STRINGIFY(x)

#define BIT_VALUE(bit) (1 << (bit))

#define SOFTWARE_VERSION_MAJOR	5
#define SOFTWARE_VERSION_MINOR	0
#define SOFTWARE_VERSION_PATCH  0

#define SOFTWARE_VERSION_STR    TO_STRING(SOFTWARE_VERSION_MAJOR) "." TO_STRING(SOFTWARE_VERSION_MINOR) "." TO_STRING(SOFTWARE_VERSION_PATCH)

#define I2C_DEVICE              "/dev/i2c-1"
#define I2C_SLAVE_ADDR          0x51
#define I2C_LOCK                "/var/lock/wittypi5_i2c.lock"

/*
 * read-only registers
 */
#define I2C_FW_ID                   0   // [0x00] Firmware id
#define I2C_FW_VERSION_MAJOR		1   // [0x01] Firmware major version
#define I2C_FW_VERSION_MINOR		2	// [0x02] Firmware minor version (x100)
                                                 
#define I2C_VUSB_MV_MSB				3	// [0x03] Most significant byte of Vusb (unit: mV)
#define I2C_VUSB_MV_LSB				4	// [0x04] Least significant byte of Vusb (unit: mV)
#define I2C_VIN_MV_MSB				5	// [0x05] Most significant byte of Vin (unit: mV)
#define I2C_VIN_MV_LSB				6	// [0x06] Least significant byte of Vin (unit: mV)
#define I2C_VOUT_MV_MSB				7	// [0x07] Most significant byte of Vout (unit: mV)
#define I2C_VOUT_MV_LSB				8	// [0x08] Least significant byte of Vout (unit: mV)
#define I2C_IOUT_MA_MSB				9	// [0x09] Most significant byte of Iout (unit: mA)
#define I2C_IOUT_MA_LSB				10	// [0x0A] Least significant byte of Iout (unit: mA)
                                                 
#define I2C_POWER_MODE			    11  // [0x0B] Power mode: 0=via Vusb, 1=via Vin
                                                 
#define I2C_MISSED_HEARTBEAT        12  // [0x0C] Missed heartbeat count
                                                 
#define I2C_RPI_STATE               13  // [0x0D] Current Raspberry Pi state: 0=OFF, 1=STARTING, 2=ON, 3=STOPPING

#define I2C_ACTION_REASON           14  // [0x0E] The latest action reason (see ACTION_REASON_???). Higher 4 bits for startup and lower 4 bits for shutdown.
 
#define I2C_MISC					15	// [0x0F] Miscellaneous state: b0-schedule script in use

/*
 * readable/writable registers
 */
#define I2C_CONF_FIRST              16  // ------

#define I2C_CONF_ADDRESS            16  // [0x10] I2C slave address: defaul=0x51
                                                 
#define I2C_CONF_DEFAULT_ON_DELAY   17  // [0x11] The delay (in second) between power connection and turning on Pi: default=255(off)
#define I2C_CONF_POWER_CUT_DELAY    18  // [0x12] The delay (in second) between Pi shutdown and power cut: default=15
                                                 
#define I2C_CONF_PULSE_INTERVAL     19  // [0x13] Pulse interval in seconds, for LED and dummy load: default=5
#define I2C_CONF_BLINK_LED          20  // [0x14] How long the white LED should stay on (in ms), 0 if white LED should not blink.
#define I2C_CONF_DUMMY_LOAD         21  // [0x15] How long the dummy load should be applied (in ms), 0 if dummy load is off.
                                                 
#define I2C_CONF_LOW_VOLTAGE        22  // [0x16] Low voltage threshold (x10), 0=disabled
#define I2C_CONF_RECOVERY_VOLTAGE   23  // [0x17] Voltage (x10) that triggers recovery, 0=disabled
                                                 
#define I2C_CONF_PS_PRIORITY        24  // [0x18] Power source priority, 0=Vusb first, 1=Vin first
                                                 
#define I2C_CONF_ADJ_VUSB           25  // [0x19] Adjustment for measured Vusb (x100), range from -127 to 127
#define I2C_CONF_ADJ_VIN            26  // [0x1A] Adjustment for measured Vin (x100), range from -127 to 127
#define I2C_CONF_ADJ_VOUT           27  // [0x1B] Adjustment for measured Vout (x100), range from -127 to 127
#define I2C_CONF_ADJ_IOUT           28  // [0x1C] Adjustment for measured Iout (x1000), range from -127 to 127
                                                 
#define I2C_CONF_WATCHDOG           29  // [0x1D] Allowed missing heartbeats before power cycle by watchdog, default=0(disable watchdog)
                                                 
#define I2C_CONF_LOG_TO_FILE		30	// [0x1E] Whether to write log into file: 1=allowed, 0=not allowed
                                                 
#define I2C_CONF_BOOTSEL_FTY_RST	31	// [0x1F] Whether to allow long press BOOTSEL and then click button for factory reset: 1=allowed, 0=not allowed
                                                 
#define I2C_CONF_ALARM1_SECOND      32  // [0x20] Second register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_MINUTE      33  // [0x21] Minute register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_HOUR        34  // [0x22] Hour register for startup alarm (BCD format)
#define I2C_CONF_ALARM1_DAY         35  // [0x23] Day register for startup alarm (BCD format)
                                                 
#define I2C_CONF_ALARM2_SECOND      36  // [0x24] Second register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_MINUTE      37  // [0x25] Minute register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_HOUR        38  // [0x26] Hour register for shutdown alarm (BCD format)
#define I2C_CONF_ALARM2_DAY         39  // [0x27] Day register for shutdown alarm (BCD format)
                                                 
#define I2C_CONF_BELOW_TEMP_ACTION  40  // [0x28] Action for below temperature: 0-do nothing; 1-startup; 2-shutdown
#define I2C_CONF_BELOW_TEMP_POINT   41  // [0x29] Set point for below temperature (signed degrees of Celsius)
#define I2C_CONF_OVER_TEMP_ACTION   42  // [0x2A] Action for over temperature: 0-do nothing; 1-startup; 2-shutdown
#define I2C_CONF_OVER_TEMP_POINT    43  // [0x2B] Set point for over temperature (signed degrees of Celsius)
                                                 
#define I2C_CONF_DST_OFFSET         44  // [0x2C] b7=mode; b6~b0: DST offset in minute, default=0(disable DST)
#define I2C_CONF_DST_BEGIN_MON      45  // [0x2D] DST begin month in BCD format
#define I2C_CONF_DST_BEGIN_DAY      46  // [0x2E] mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define I2C_CONF_DST_BEGIN_HOUR     47  // [0x2F] DST begin hour in BCD format
#define I2C_CONF_DST_BEGIN_MIN      48  // [0x30] DST begin minute in BCD format
#define I2C_CONF_DST_END_MON        49  // [0x31] DST end month in BCD format
#define I2C_CONF_DST_END_DAY        50  // [0x32] mode=0: b7~b4=week in BCD, b3~b0=day in BCD; mode=1: b7~b0=date in BCD
#define I2C_CONF_DST_END_HOUR       51  // [0x33] DST end hour in BCD format
#define I2C_CONF_DST_END_MIN        52  // [0x34] DST end minute in BCD format
#define I2C_CONF_DST_APPLIED        53  // [0x35] Whether DST has been applied
                                                 
#define I2C_CONF_SYS_CLOCK_MHZ		54	// [0x36] System clock (in MHz) for RP2350: default=48 (required for USB drive and USB-uart)

#define I2C_CONF_LAST               63  // ------
#define I2C_ADMIN_FIRST             64  // ------

#define I2C_ADMIN_DIR  		        64  // [0x40] Register to specify directory
#define I2C_ADMIN_CONTEXT  		    65  // [0x41] Register to provide extra context
#define I2C_ADMIN_DOWNLOAD		    66  // [0x42] Register to provide download stream
#define I2C_ADMIN_UPLOAD		    67  // [0x43] Register to provide upload stream
                                                 
#define I2C_ADMIN_PASSWORD		    68  // [0x44] Password for administrative command, will be cleared automatically after running the command
#define I2C_ADMIN_COMMAND		    69  // [0x45] Administrative command to run, will be cleared automatically after running the command
                                                 
#define I2C_ADMIN_HEARTBEAT         70  // [0x46] Heartbeat register for watchdog
                                                 
#define I2C_ADMIN_SHUTDOWN          71  // [0x47] Register for shutdown request

#define I2C_ADMIN_LAST              79  // ------


/*
 * virtual registers (mapped to RX8025 or TMP112)
 */
#define I2C_VREG_FIRST                      80  // ------

#define I2C_VREG_RX8025_SEC					80  // [0x50] Second in RTC time (0~59)        
#define I2C_VREG_RX8025_MIN					81  // [0x51] Minute in RTC time (0~59)        
#define I2C_VREG_RX8025_HOUR				82  // [0x52] Hour in RTC time (0~23)          
#define I2C_VREG_RX8025_WEEKDAY				83  // [0x53] Weekday in RTC time (0~6)        
#define I2C_VREG_RX8025_DAY					84  // [0x54] Date in RTC time (1~28/29/30/31) 
#define I2C_VREG_RX8025_MONTH				85  // [0x55] Month in RTC time (1~12)         
#define I2C_VREG_RX8025_YEAR				86  // [0x56] Year in RTC time (0~99)          
#define I2C_VREG_RX8025_RAM					87  // [0x57] RTC RAM register                 
#define I2C_VREG_RX8025_MIN_ALARM			88  // [0x58] RTC minute alarm register        
#define I2C_VREG_RX8025_HOUR_ALARM			89  // [0x59] RTC hour alarm register          
#define I2C_VREG_RX8025_DAY_ALARM			90  // [0x5A] RTC day alarm register           
#define I2C_VREG_RX8025_TIMER_COUNTER0		91  // [0x5B] RTC timer Counter 0              
#define I2C_VREG_RX8025_TIMER_COUNTER1		92  // [0x5C] RTC timer Counter 1              
#define I2C_VREG_RX8025_EXTENSION_REGISTER	93  // [0x5D] RTC extension register           
#define I2C_VREG_RX8025_FLAG_REGISTER		94  // [0x5E] RTC flag register                
#define I2C_VREG_RX8025_CONTROL_REGISTER	95  // [0x5F] RTC control register             
                                                                                           
#define I2C_VREG_TMP112_TEMP_MSB			96  // [0x60] MSB of temperature               
#define I2C_VREG_TMP112_TEMP_LSB			97  // [0x61] LSB of temperature               
#define I2C_VREG_TMP112_CONF_MSB			98  // [0x62] MSB of configuration             
#define I2C_VREG_TMP112_CONF_LSB			99  // [0x63] LSB of configuration             
#define I2C_VREG_TMP112_TLOW_MSB			100 // [0x64] MSB of low-temperature threshold 
#define I2C_VREG_TMP112_TLOW_LSB			101 // [0x65] LSB of low-temperature threshold 
#define I2C_VREG_TMP112_THIGH_MSB			102 // [0x66] MSB of high-temperature threshold
#define I2C_VREG_TMP112_THIGH_LSB			103 // [0x67] LSB of high-temperature threshold

#define I2C_VREG_LAST                       103 // ------


/*
 * I2C administrative command form: 2 bytes (password + command)
 * When writing it via I2C, make sure to write password byte first
 */
#define I2C_ADMIN_PWD_CMD_PRINT_PRODUCT_INFO        0x17F0
#define I2C_ADMIN_PWD_CMD_FORMAT_DISK               0x37FD
#define I2C_ADMIN_PWD_CMD_RESET_RTC                 0x387C
#define I2C_ADMIN_PWD_CMD_ENABLE_ID_EEPROM_WP       0x81EE
#define I2C_ADMIN_PWD_CMD_DISABLE_ID_EEPROM_WP      0x82ED
#define I2C_ADMIN_PWD_CMD_RESET_CONF                0x945B
#define I2C_ADMIN_PWD_CMD_SYNC_CONF                 0x955C
#define I2C_ADMIN_PWD_CMD_SAVE_LOG                  0x975D
#define I2C_ADMIN_PWD_CMD_LOAD_SCRIPT               0x9915
#define I2C_ADMIN_PWD_CMD_LIST_FILES                0xA0F1
#define I2C_ADMIN_PWD_CMD_CHOOSE_SCRIPT             0xA159
#define I2C_ADMIN_PWD_CMD_PURGE_SCRIPT              0xA260


/*
 * Reason for latest action (used by I2C_ACTION_REASON register)
 */
#define ACTION_REASON_UNKNOWN               0
#define ACTION_REASON_ALARM1                1
#define ACTION_REASON_ALARM2                2
#define ACTION_REASON_BUTTON_CLICK          3
#define ACTION_REASON_VIN_DROP              4
#define ACTION_REASON_VIN_RECOVER           5
#define ACTION_REASON_OVER_TEMPERATURE      6
#define ACTION_REASON_BELOW_TEMPERATURE     7
#define ACTION_REASON_POWER_CONNECTED       8
#define ACTION_REASON_REBOOT                9
#define ACTION_REASON_MISSED_HEARTBEAT      10
#define ACTION_REASON_EXTERNAL_SHUTDOWN     11
#define ACTION_REASON_EXTERNAL_REBOOT       12


/*
 * I2C file access
 */
#define DIRECTORY_NONE			0
#define DIRECTORY_ROOT			1
#define DIRECTORY_CONF			2
#define DIRECTORY_LOG			3
#define DIRECTORY_SCHEDULE		4

#define LIST_BEGIN              '<'
#define LIST_DELIMITER          '|'
#define LIST_END                '>'


#define FW_ID_WITTYPI_5         0x51
#define FW_ID_WITTYPI_5_MINI    0x52
#define FW_ID_WITTYPI_5_L3V7    0x53

#define MODEL_UNKNOWN           0
#define MODEL_WITTYPI_5         1
#define MODEL_WITTYPI_5_MINI    2
#define MODEL_WITTYPI_5_L3V7    3

#define POWER_VIA_USB           0
#define POWER_VIA_VIN           1

#define TEMP_ACTION_NONE		0
#define TEMP_ACTION_STARTUP		1
#define TEMP_ACTION_SHUTDOWN	2

#define PACKET_BEGIN            '<'
#define PACKET_DELIMITER        '|'
#define PACKET_END              '>'
#define CRC8_POLYNOMIAL			0x31	// CRC-8 Polynomial (x^8 + x^5 + x^4 + 1 -> 00110001 -> 0x31)

#define SCHEDULED_DATETIME_BUFFER_SIZE	12


// Log mode
typedef enum {
    LOG_WITH_TIME,
    LOG_WITHOUT_TIME,
    LOG_NONE,
} LogMode;

// DateTime structure
typedef struct {
    int16_t year;   // 2000~2099
    int8_t month;   // 1~12
    int8_t day;     // 1~28/29/30/31
    int8_t hour;    // 0~23
    int8_t min;     // 0~59
    int8_t sec;     // 0~59
    int8_t wday;    // 0~6 (Sunday~Saturday)
} DateTime;

// Witty Pi 5 models
extern const char *wittypi_models[];
extern const int wittypi_models_count;

// Action (startup/shutdown) reasons
extern const char *action_reasons[];
extern const int action_reasons_count;


/**
 * Get actual value for a binary-coded decimal (BCD) byte
 * 
 * @param bcd The BCD byte
 * @return The actual value
 */
static inline uint8_t bcd_to_dec(uint8_t bcd) {
	return ((bcd >> 4) * 10) + (bcd & 0x0F);
}


/**
 * Get binary-coded decimal (BCD) byte for a value
 * 
 * @param dec The value
 * @return The binary-coded decimal (BCD) byte
 */
static inline uint8_t dec_to_bcd(uint8_t dec) {
    return ((dec / 10) << 4) | (dec % 10);
}


/**
 * Set log mode
 * 
 * @param mode The log mode to be used
 */
void set_log_mode(LogMode mode);


/**
 * Print log function that accepts same arguments as printf
 * 
 * @param format Format string in printf style
 * @param ... Variable arguments for the format string
 * @return Number of characters printed (excluding null byte)
 */
int print_log(const char *format, ...);


/**
 * Print system information
 */
void print_sys_info(void);


/**
 * Print Raspberry Pi information
 */
void print_pi_info(void);


/**
 * Calculates the CRC-8 checksum for a data buffer
 *
 * @param data Pointer to the data buffer
 * @param len Length of the data buffer (in bytes)
 * @return uint8_t The calculated CRC-8 checksum (single byte)
 */
uint8_t calculate_crc8(const uint8_t *data, size_t len);


/**
 * Open I2C device
 * 
 * @return The handler of the device if open succesfully, -1 otherwise
 */
int open_i2c_device(void);


/**
 * Read value from I2C register, with or without validation
 * When reading value that may change quickly, validation should not be used
 *
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param validate Whether to validate the value
 * @return The value if read succesfully, -1 otherwise
 */
int i2c_get_impl(int i2c_dev, uint8_t index, bool validate);


/**
 * Read value from I2C register with validation
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @return The value if read succesfully, -1 otherwise
 */
int i2c_get(int i2c_dev, uint8_t index);


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
int i2c_read_stream_util(int i2c_dev, uint8_t index, uint8_t * buf, int size, uint8_t expected);


/**
 * Write value to I2C register, with or without validation
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param value The value of the register
 * @param validate Whether to validate the value
 * @return true if successfully written, false otherwise
 */
bool i2c_set_impl(int i2c_dev, uint8_t index, uint8_t value, bool validate);


/**
 * Write value to I2C register with validation
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the register
 * @param value The value of the register
 * @return true if successfully written, false otherwise
 */
bool i2c_set(int i2c_dev, uint8_t index, uint8_t value);


/**
 * Write data to specific I2C register until expected value appear
 * 
 * @param i2c_dev The I2C device handler, use -1 to get one internally
 * @param index The index of the stream register
 * @param buf The pointer to the buffer
 * @param size The size of buffer
 * @param expected The byte that would stop the writing
 * @return The length of data read, -1 if error
 */
int i2c_write_stream_util(int i2c_dev, uint8_t index, uint8_t * buf, int size, uint8_t expected);


/**
 * Close I2C device
 *
 * @param i2c_dev The I2C device handler
 */
void close_i2c_device(int i2c_dev);


/**
 * Get Witty Pi model
 * 
 * @return The model of Witty Pi
 */
int get_wittypi_model(void);


/**
 * Get power mode
 * 
 * @return 0 if powered via USB, 1 if powered via VIN, 255 if not powered, -1 if error
 */
int get_power_mode(void);


/**
 * Get temperature
 * 
 * @return Temperature in Celsius degree
 */
float get_temperature(void);


/**
 * Convert temperature from Celsius to Fahrenheit
 * 
 * @param celsius Temperature in Celsius degree
 * @return Temperature in Fahrenheit degree
 */
float celsius_to_fahrenheit(float celsius);


/**
 * Get input voltage (Vin)
 * 
 * @return Input voltage
 */
float get_vin(void);


/**
 * Get USB-C voltage (Vusb)
 * 
 * @return USB-C voltage
 */
float get_vusb(void);


/**
 * Get output voltage (Vout)
 * 
 * @return Output voltage
 */
float get_vout(void);


/**
 * Get output current (Iout)
 * 
 * @return Output current
 */
float get_iout(void);


/**
 * Get local system time information
 * 
 * @param dt The DateTime object to save the result
 * @return true if succeed, otherwise false
 */
bool get_system_time(DateTime * dt);


/**
 * Get RTC time information
 * 
 * @param dt The DateTime object to save the result
 * @return true if succeed, otherwise false
 */
bool get_rtc_time(DateTime * dt);


/**
 * Check if the time stored in DateTime is valid
 * 
 * @param dt The DateTime object to save the time
 * @return true if valid, otherwise false
 */
bool is_time_valid(DateTime * dt);


/**
 * Write system time into RTC
 * 
 * @return true if succeed, otherwise false
 */
bool system_to_rtc(void);


/**
 * Write RTC time into system
 * 
 * @return true if succeed, otherwise false
 */
bool rtc_to_system(void);


/**
 * Write network time into system and RTC
 * 
 * @return true if succeed, otherwise false
 */
bool network_to_system_and_rtc(void);


/**
 * Get scheduled startup time as individual components
 * 
 * @param date Pointer to store the day value (1-31)
 * @param hour Pointer to store the hour value (0-23)
 * @param minute Pointer to store the minute value (0-59)
 * @param second Pointer to store the second value (0-59)
 * @return true if succeed, false if fail or no valid startup has been scheduled
 */
bool get_startup_time(uint8_t * date, uint8_t * hour, uint8_t * minute, uint8_t * second);


/**
 * Get scheduled shutdown time as individual components
 * 
 * @param date Pointer to store the day value (1-31)
 * @param hour Pointer to store the hour value (0-23)
 * @param minute Pointer to store the minute value (0-59)
 * @param second Pointer to store the second value (0-59)
 * @return true if succeed, false if fail or no valid shutdown has been scheduled
 */
bool get_shutdown_time(uint8_t * date, uint8_t * hour, uint8_t * minute, uint8_t * second);


/**
 * Set scheduled startup time
 * 
 * @param date The date (1-31)
 * @param hour The hour (0-23)
 * @param minute The minute (0-59)
 * @param second The second (0-59)
 * @return true if succeed, false if fail
 */
bool set_startup_time(uint8_t date, uint8_t hour, uint8_t minute, uint8_t second);


/**
 * Clear scheduled startup time
 * 
 * @return true if succeed, false if fail
 */
bool clear_startup_time();


/**
 * Set scheduled shutdown time
 * 
 * @param date The date (1-31)
 * @param hour The hour (0-23)
 * @param minute The minute (0-59)
 * @param second The second (0-59)
 * @return true if succeed, false if fail
 */
bool set_shutdown_time(uint8_t date, uint8_t hour, uint8_t minute, uint8_t second);


/**
 * Clear scheduled shutdown time
 * 
 * @return true if succeed, false if fail
 */
bool clear_shutdown_time();


/**
 * Get low voltage threshold in Volt
 * 
 * @return The low voltage threshold in Volt, -1 if fail
 */
float get_low_voltage_threshold(void);


/**
 * Set low voltage threshold in Volt
 * 
 * @param threshold Low voltage threshold in Volt
 * @return true if succeed, false if fail
 */
bool set_low_voltage_threshold(float threshold);


/**
 * Get recovery voltage threshold in Volt
 * 
 * @return The low voltage threshold in Volt, -1 if fail
 */
float get_recovery_voltage_threshold(void);


/**
 * Set recovery voltage threshold in Volt
 * 
 * @param threshold Low voltage threshold in Volt
 * @return true if succeed, false if fail
 */
bool set_recovery_voltage_threshold(float threshold);


/**
 * Run administrative command
 * 
 * @param psw_cmd The 16 bit integer that stores password and command
 * @return true if succeed, false if fail
 */
bool run_admin_command(uint16_t psw_cmd);


/**
 * Check if schedule script is in use
 * 
 * @return true if is in use, otherwise false
 */
bool is_script_in_use(void);


/**
 * Get the reason for recent startup
 * 
 * @return the reason code
 */
int get_startup_reason(void);


/**
 * Get the reason for recent shutdown
 * 
 * @return the reason code
 */
int get_shutdown_reason(void);


#endif  /* __WP5LIB_H */