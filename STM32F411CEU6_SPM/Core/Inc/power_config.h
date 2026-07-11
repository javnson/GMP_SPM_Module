#ifndef POWER_CONFIG_H
#define POWER_CONFIG_H

/* All voltages are expressed in millivolts and all times in milliseconds. */
#define POWER_TASK_PERIOD_MS                  10U
#define POWER_TELEMETRY_PERIOD_MS          2000U
#define POWER_STARTUP_GRACE_MS             2000U
#define POWER_INPUT_DEBOUNCE_MS              80U
#define POWER_TRANSFER_OVERLAP_MS           200U
#define POWER_BUTTON_DEBOUNCE_MS             30U

#define POWER_ADC_REFERENCE_MV             3300U
#define POWER_ADC_FULL_SCALE               4095U
#define POWER_ADC_AVERAGE_SAMPLES             8U
#define POWER_ADC_DIVIDER_TOP_OHM          30900U
#define POWER_ADC_DIVIDER_BOTTOM_OHM        5100U

#define POWER_INPUT_UV_TRIP_MV             10500U
#define POWER_INPUT_UV_RECOVER_MV          11000U
#define POWER_OUTPUT_UV_TRIP_MV            10500U
#define POWER_OUTPUT_UV_RECOVER_MV         11000U
#define POWER_OUTPUT_OV_TRIP_MV            13000U
#define POWER_OUTPUT_OV_RECOVER_MV         12600U
#define POWER_OUTPUT_ENABLE_MV             10500U
#define POWER_OUTPUT_DISABLE_MV             8000U

#define POWER_SC_RATED_FULL_MV             10730U
#define POWER_SC_CHARGE_COMPLETE_MV        10500U
#define POWER_SC_CHARGE_REARM_MV           10200U
#define POWER_SC_CHARGE_HOLD_MS            30000U
#define POWER_SC_MANUAL_TEST_STOP_MV        5000U
#define POWER_SC_BACKUP_CUTOFF_MV           4500U

#define POWER_RAPID_DROP_MV                 2000U
#define POWER_RAPID_DROP_WINDOW_MS           100U
#define POWER_RAPID_DROP_HOLD_MS            2000U

#define POWER_EEPROM_I2C_ADDRESS          (0x50U << 1U)
#define POWER_EEPROM_TIMEOUT_MS              50U
#define POWER_EEPROM_WRITE_CYCLE_MS           6U

#endif /* POWER_CONFIG_H */
