#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>

#include "wp5lib.h"


#define ADMIN_TURN_RPI_OFF      1
#define ADMIN_RPI_POWERING_OFF  2
#define ADMIN_RPI_REBOOTING     3

#define SHUTDOWN_CMD            "sudo shutdown -h now"

#define PID_FILE_PATH           "/run/wp5d.pid"


bool running = true;

int i2c_dev = -1;


/**
 * Signal handler
 */
void handle_signal(int signum) {
    
    print_log("Caught signal %d\n", signum);
    
    running = false;
    
    close_i2c_device(i2c_dev);
    
    print_log("Exit now.\n");
    
    exit(0);
}


/**
 * Main function
 */
int main(int argc, char *argv[]) {

    // Check for existing instance
    FILE *fp;
    pid_t existing_pid;
    char line[10];
    fp = fopen(PID_FILE_PATH, "r");
    if (fp != NULL) {
        if (fgets(line, sizeof(line), fp) != NULL) {
            existing_pid = atoi(line);
            fclose(fp);

            if (kill(existing_pid, 0) == 0) {
                if (kill(existing_pid, SIGINT) == 0) {
                    print_log("Sent SIGINT to PID=%d\n", existing_pid);
                } else {
                    print_log("Sending SIGINT failed.\n");
                }
            } else {
                print_log("PID file is outdated, deleting it...\n", PID_FILE_PATH, existing_pid);
                unlink(PID_FILE_PATH);
            }
        } else {
            print_log("Can not read PID file, deleting it...\n", PID_FILE_PATH);
            fclose(fp);
            unlink(PID_FILE_PATH);
        }
    }

    // Process --poweroff and --reboot arguments
    for (int i = 1; i < argc; i ++) {
        if (strcmp(argv[i], "--poweroff") == 0) {
            i2c_set(-1, I2C_ADMIN_SHUTDOWN, ADMIN_RPI_POWERING_OFF);
            exit(0);
        } 
        if (strcmp(argv[i], "--reboot") == 0) {
            i2c_set(-1, I2C_ADMIN_SHUTDOWN, ADMIN_RPI_REBOOTING);
            exit(0);
        }
    }
    
    // Save PID to file
    pid_t current_pid = getpid();
    print_log("Witty Pi 5 daemon V%s started. PID = %d\n", SOFTWARE_VERSION_STR, current_pid);
    fp = fopen(PID_FILE_PATH, "w");
    if (fp == NULL) {
        print_log("Can not write to PID file: %s\n", PID_FILE_PATH);
        exit(EXIT_FAILURE);
    }
    fprintf(fp, "%d", current_pid);
    fclose(fp);
    
    // Register signal handler
    signal(SIGINT, handle_signal);

    // Print system information
    print_sys_info();
    
    // Print Raspberry Pi information
    print_pi_info();
    
    // Main loop
    i2c_dev = open_i2c_device();
    int cur_model = MODEL_UNKNOWN;
    while (running) {
        sleep(1);
        
        // Try to connect Witty Pi
        int model = get_wittypi_model();
        if (model != cur_model && model > MODEL_UNKNOWN && model < wittypi_models_count) {
            cur_model = model;
            print_log("Connected to %s\n", wittypi_models[model]);
            
            // Synchronize time once
            DateTime dt;
            if (get_rtc_time(&dt) && is_time_valid(&dt)) {
                print_log("RTC has valid time, write RTC time into system...\n");
                if (rtc_to_system()) {
                    print_log("Done :)\n");
                } else {
                    print_log("Failed :(\n");
                }
            } else {
                print_log("RTC has invalid time, write system time into RTC...\n");
                if (system_to_rtc()) {
                    print_log("Done :)\n");
                } else {
                    print_log("Failed :(\n");
                }
            }
            
            // Print startup reason once
            int reason = get_startup_reason();
            print_log("Startup reason: %s\n", action_reasons[reason >= action_reasons_count ? ACTION_REASON_UNKNOWN : reason]);
        }
        if (model == MODEL_UNKNOWN) {   // Witty Pi not detected, retry later
            continue;
        }
       
        if (i2c_get(i2c_dev, I2C_ADMIN_SHUTDOWN) == ADMIN_TURN_RPI_OFF) {   // Poll for shutdown request / implicitly send heartbeat
            print_log("Detected shutdown request, clearing and shutdown...\n");
                    
            if (!i2c_set(i2c_dev, I2C_ADMIN_SHUTDOWN, 0)) {
                print_log("Failed clearing shutdown request.\n");
            }
            
            // Print shutdown reason
            int reason = get_shutdown_reason();
            print_log("Shutdown reason: %s\n", action_reasons[reason >= action_reasons_count ? ACTION_REASON_UNKNOWN : reason]);
            
            // Shutdown the system
            system(SHUTDOWN_CMD);
            break;
        }
    }
    
    // Clean up and exit
    close_i2c_device(i2c_dev);
    return 0;
}
