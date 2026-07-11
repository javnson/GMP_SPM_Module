#include "power_control.h"

#include "cmsis_os2.h"
#include "main.h"
#include "power_config.h"
#include "usbd_cdc_if.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

typedef enum
{
  POWER_MODE_NORMAL = 0,
  POWER_MODE_BACKUP,
  POWER_MODE_MANUAL_TEST,
  POWER_MODE_TRANSFER_TO_MAIN
} PowerMode;

enum
{
  POWER_FAULT_INPUT_LOST = (1UL << 0),
  POWER_FAULT_OUTPUT_LOW = (1UL << 1),
  POWER_FAULT_OUTPUT_HIGH = (1UL << 2),
  POWER_FAULT_SC_DEPLETED = (1UL << 3),
  POWER_FAULT_RAPID_DROP = (1UL << 4),
  POWER_FAULT_ADC = (1UL << 5)
};

typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  GPIO_PinState raw;
  GPIO_PinState stable;
  uint32_t changed_at;
} Button;

typedef struct
{
  uint32_t magic;
  uint32_t version;
  uint32_t fault_count;
  uint32_t backup_count;
  uint32_t last_faults;
  uint32_t checksum;
} PersistentRecord;

typedef struct
{
  ADC_HandleTypeDef *hadc;
  I2C_HandleTypeDef *hi2c;
  uint32_t input_mv;
  uint32_t bus_mv;
  uint32_t sc_mv;
  uint32_t faults;
  uint32_t previous_faults;
  uint32_t alarm_faults;
  uint32_t transfer_started_at;
  uint32_t charge_full_since;
  uint32_t rapid_drop_until;
  uint32_t telemetry_at;
  uint32_t input_invalid_since;
  uint32_t input_valid_since;
  uint32_t rapid_drop_reference_mv;
  uint32_t rapid_drop_reference_at;
  bool input_valid;
  bool output_low;
  bool output_high;
  bool output_enabled;
  bool charge_requested;
  bool charge_enabled;
  bool alarm_muted;
  bool adc_failed;
  bool measurements_initialized;
  PowerMode mode;
  Button mute_button;
  Button test_button;
  Button charge_button;
  PersistentRecord record;
} PowerContext;

#define POWER_RECORD_MAGIC 0x53504D31UL
#define POWER_RECORD_VERSION 1UL

static PowerContext power;

static uint32_t RecordChecksum(const PersistentRecord *record)
{
  return record->magic ^ record->version ^ record->fault_count ^
         record->backup_count ^ record->last_faults ^ 0xA55A3CC3UL;
}

static void RecordLoad(void)
{
  PersistentRecord loaded;

  memset(&loaded, 0, sizeof(loaded));
  if (HAL_I2C_Mem_Read(power.hi2c, POWER_EEPROM_I2C_ADDRESS, 0U,
                       I2C_MEMADD_SIZE_8BIT, (uint8_t *)&loaded,
                       sizeof(loaded), POWER_EEPROM_TIMEOUT_MS) == HAL_OK &&
      loaded.magic == POWER_RECORD_MAGIC &&
      loaded.version == POWER_RECORD_VERSION &&
      loaded.checksum == RecordChecksum(&loaded))
  {
    power.record = loaded;
  }
  else
  {
    memset(&power.record, 0, sizeof(power.record));
    power.record.magic = POWER_RECORD_MAGIC;
    power.record.version = POWER_RECORD_VERSION;
    power.record.checksum = RecordChecksum(&power.record);
  }
}

static void RecordSave(void)
{
  uint8_t *bytes = (uint8_t *)&power.record;

  power.record.checksum = RecordChecksum(&power.record);
  (void)HAL_I2C_Mem_Write(power.hi2c, POWER_EEPROM_I2C_ADDRESS, 0U,
                          I2C_MEMADD_SIZE_8BIT, bytes, 16U,
                          POWER_EEPROM_TIMEOUT_MS);
  osDelay(POWER_EEPROM_WRITE_CYCLE_MS);
  (void)HAL_I2C_Mem_Write(power.hi2c, POWER_EEPROM_I2C_ADDRESS, 16U,
                          I2C_MEMADD_SIZE_8BIT, &bytes[16],
                          sizeof(power.record) - 16U, POWER_EEPROM_TIMEOUT_MS);
  osDelay(POWER_EEPROM_WRITE_CYCLE_MS);
}

static uint32_t ReadAdcMillivolts(uint32_t channel)
{
  ADC_ChannelConfTypeDef config = {0};
  uint32_t sum = 0U;
  uint32_t index;

  config.Channel = channel;
  config.Rank = 1U;
  config.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  if (HAL_ADC_ConfigChannel(power.hadc, &config) != HAL_OK)
  {
    power.adc_failed = true;
    return 0U;
  }

  for (index = 0U; index < POWER_ADC_AVERAGE_SAMPLES; ++index)
  {
    if (HAL_ADC_Start(power.hadc) != HAL_OK ||
        HAL_ADC_PollForConversion(power.hadc, 2U) != HAL_OK)
    {
      (void)HAL_ADC_Stop(power.hadc);
      power.adc_failed = true;
      return 0U;
    }
    sum += HAL_ADC_GetValue(power.hadc);
    (void)HAL_ADC_Stop(power.hadc);
  }

  sum = (sum + (POWER_ADC_AVERAGE_SAMPLES / 2U)) / POWER_ADC_AVERAGE_SAMPLES;
  return (uint32_t)(((uint64_t)sum * POWER_ADC_REFERENCE_MV *
                    (POWER_ADC_DIVIDER_TOP_OHM + POWER_ADC_DIVIDER_BOTTOM_OHM)) /
                   ((uint64_t)POWER_ADC_FULL_SCALE * POWER_ADC_DIVIDER_BOTTOM_OHM));
}

static uint32_t FilterVoltage(uint32_t old_value, uint32_t new_value)
{
  return (old_value == 0U) ? new_value : ((old_value * 3U + new_value + 2U) / 4U);
}

static bool ButtonPressed(Button *button, uint32_t now)
{
  GPIO_PinState sample = HAL_GPIO_ReadPin(button->port, button->pin);

  if (sample != button->raw)
  {
    button->raw = sample;
    button->changed_at = now;
  }
  if (sample != button->stable && (now - button->changed_at) >= POWER_BUTTON_DEBOUNCE_MS)
  {
    button->stable = sample;
    return sample == GPIO_PIN_RESET;
  }
  return false;
}

static void SetLed(GPIO_TypeDef *port, uint16_t pin, bool on)
{
  HAL_GPIO_WritePin(port, pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void UpdateMeasurements(uint32_t now)
{
  uint32_t input_sample;
  uint32_t bus_sample;
  uint32_t sc_sample;

  power.adc_failed = false;
  sc_sample = ReadAdcMillivolts(ADC_CHANNEL_1);
  input_sample = ReadAdcMillivolts(ADC_CHANNEL_2);
  bus_sample = ReadAdcMillivolts(ADC_CHANNEL_3);
  if (!power.adc_failed)
  {
    if (!power.measurements_initialized)
    {
      power.sc_mv = sc_sample;
      power.input_mv = input_sample;
      power.bus_mv = bus_sample;
      power.input_valid = input_sample >= POWER_INPUT_UV_RECOVER_MV;
      power.measurements_initialized = true;
    }
    else
    {
      power.sc_mv = FilterVoltage(power.sc_mv, sc_sample);
      power.input_mv = FilterVoltage(power.input_mv, input_sample);
      power.bus_mv = FilterVoltage(power.bus_mv, bus_sample);
    }
  }

  if (power.input_valid)
  {
    if (power.input_mv < POWER_INPUT_UV_TRIP_MV)
    {
      if (power.input_invalid_since == 0U)
        power.input_invalid_since = now;
      if ((now - power.input_invalid_since) >= POWER_INPUT_DEBOUNCE_MS)
        power.input_valid = false;
    }
    else
    {
      power.input_invalid_since = 0U;
    }
  }
  else
  {
    if (power.input_mv >= POWER_INPUT_UV_RECOVER_MV)
    {
      if (power.input_valid_since == 0U)
        power.input_valid_since = now;
      if ((now - power.input_valid_since) >= POWER_INPUT_DEBOUNCE_MS)
        power.input_valid = true;
    }
    else
    {
      power.input_valid_since = 0U;
    }
  }

  if (!power.output_low && power.bus_mv < POWER_OUTPUT_UV_TRIP_MV)
    power.output_low = true;
  else if (power.output_low && power.bus_mv >= POWER_OUTPUT_UV_RECOVER_MV)
    power.output_low = false;

  if (!power.output_high && power.bus_mv > POWER_OUTPUT_OV_TRIP_MV)
    power.output_high = true;
  else if (power.output_high && power.bus_mv <= POWER_OUTPUT_OV_RECOVER_MV)
    power.output_high = false;

  if ((now - power.rapid_drop_reference_at) >= POWER_RAPID_DROP_WINDOW_MS)
  {
    if (power.rapid_drop_reference_mv > power.bus_mv &&
        (power.rapid_drop_reference_mv - power.bus_mv) >= POWER_RAPID_DROP_MV)
      power.rapid_drop_until = now + POWER_RAPID_DROP_HOLD_MS;
    power.rapid_drop_reference_mv = power.bus_mv;
    power.rapid_drop_reference_at = now;
  }
}

static void EnterBackup(uint32_t now)
{
  (void)now;
  if (power.mode != POWER_MODE_BACKUP)
  {
    power.mode = POWER_MODE_BACKUP;
    ++power.record.backup_count;
    RecordSave();
  }
}

static void UpdateMode(uint32_t now, bool test_pressed)
{
  if (!power.input_valid && power.mode != POWER_MODE_BACKUP)
  {
    EnterBackup(now);
  }
  else if (test_pressed && power.mode == POWER_MODE_NORMAL && power.input_valid &&
           power.sc_mv >= POWER_SC_MANUAL_TEST_STOP_MV)
  {
    power.mode = POWER_MODE_MANUAL_TEST;
  }
  else if (test_pressed && power.mode == POWER_MODE_MANUAL_TEST)
  {
    power.mode = POWER_MODE_TRANSFER_TO_MAIN;
    power.transfer_started_at = now;
  }

  if (power.mode == POWER_MODE_BACKUP && power.input_valid)
  {
    power.mode = POWER_MODE_TRANSFER_TO_MAIN;
    power.transfer_started_at = now;
  }
  else if (power.mode == POWER_MODE_MANUAL_TEST &&
           power.sc_mv < POWER_SC_MANUAL_TEST_STOP_MV)
  {
    if (power.input_valid)
    {
      power.mode = POWER_MODE_TRANSFER_TO_MAIN;
      power.transfer_started_at = now;
    }
    else
    {
      EnterBackup(now);
    }
  }
  else if (power.mode == POWER_MODE_TRANSFER_TO_MAIN)
  {
    if (!power.input_valid)
      EnterBackup(now);
    else if ((now - power.transfer_started_at) >= POWER_TRANSFER_OVERLAP_MS)
      power.mode = POWER_MODE_NORMAL;
  }

  switch (power.mode)
  {
    case POWER_MODE_NORMAL:
      HAL_GPIO_WritePin(CTRL_DC_INPUT_GPIO_Port, CTRL_DC_INPUT_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(SC_OUTPUT_EN_GPIO_Port, SC_OUTPUT_EN_Pin, GPIO_PIN_RESET);
      break;
    case POWER_MODE_TRANSFER_TO_MAIN:
      HAL_GPIO_WritePin(CTRL_DC_INPUT_GPIO_Port, CTRL_DC_INPUT_Pin, GPIO_PIN_SET);
      HAL_GPIO_WritePin(SC_OUTPUT_EN_GPIO_Port, SC_OUTPUT_EN_Pin, GPIO_PIN_SET);
      break;
    case POWER_MODE_BACKUP:
    case POWER_MODE_MANUAL_TEST:
    default:
      HAL_GPIO_WritePin(CTRL_DC_INPUT_GPIO_Port, CTRL_DC_INPUT_Pin, GPIO_PIN_RESET);
      HAL_GPIO_WritePin(SC_OUTPUT_EN_GPIO_Port, SC_OUTPUT_EN_Pin,
                        power.sc_mv >= POWER_SC_BACKUP_CUTOFF_MV ? GPIO_PIN_SET : GPIO_PIN_RESET);
      break;
  }
}

static void UpdateCharge(uint32_t now, bool charge_pressed)
{
  if (charge_pressed)
  {
    power.charge_requested = !power.charge_requested;
    power.charge_full_since = 0U;
  }

  if (power.sc_mv < POWER_SC_CHARGE_REARM_MV)
    power.charge_full_since = 0U;

  power.charge_enabled = power.charge_requested && power.input_valid &&
                         power.mode == POWER_MODE_NORMAL;
  if (power.charge_enabled && power.sc_mv >= POWER_SC_CHARGE_COMPLETE_MV)
  {
    if (power.charge_full_since == 0U)
      power.charge_full_since = now;
    else if ((now - power.charge_full_since) >= POWER_SC_CHARGE_HOLD_MS)
    {
      power.charge_requested = false;
      power.charge_enabled = false;
    }
  }
  HAL_GPIO_WritePin(SC_CHARGE_EN_GPIO_Port, SC_CHARGE_EN_Pin,
                    power.charge_enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void UpdateFaults(uint32_t now, bool mute_pressed)
{
  uint32_t alarm_faults;

  power.faults = 0U;
  if (now >= POWER_STARTUP_GRACE_MS)
  {
    if (!power.input_valid)
      power.faults |= POWER_FAULT_INPUT_LOST;
    if (power.output_low)
      power.faults |= POWER_FAULT_OUTPUT_LOW;
    if (power.output_high)
      power.faults |= POWER_FAULT_OUTPUT_HIGH;
    if ((power.mode == POWER_MODE_BACKUP || power.mode == POWER_MODE_MANUAL_TEST) &&
        power.sc_mv < POWER_SC_BACKUP_CUTOFF_MV)
      power.faults |= POWER_FAULT_SC_DEPLETED;
    if ((int32_t)(power.rapid_drop_until - now) > 0)
      power.faults |= POWER_FAULT_RAPID_DROP;
    if (power.adc_failed)
      power.faults |= POWER_FAULT_ADC;
  }

  alarm_faults = power.faults & (POWER_FAULT_INPUT_LOST | POWER_FAULT_OUTPUT_LOW |
                                  POWER_FAULT_OUTPUT_HIGH);
  if ((alarm_faults & ~power.alarm_faults) != 0U)
    power.alarm_muted = false;
  if (alarm_faults == 0U)
    power.alarm_muted = false;
  else if (mute_pressed)
    power.alarm_muted = true;
  power.alarm_faults = alarm_faults;
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin,
                    (alarm_faults != 0U && !power.alarm_muted) ? GPIO_PIN_SET : GPIO_PIN_RESET);

  if (power.faults != 0U && power.previous_faults == 0U)
  {
    ++power.record.fault_count;
    power.record.last_faults = power.faults;
    RecordSave();
  }
  power.previous_faults = power.faults;
}

static void UpdateOutputEnable(void)
{
  if (!power.output_enabled && power.bus_mv >= POWER_OUTPUT_ENABLE_MV && !power.output_high)
    power.output_enabled = true;
  else if (power.output_enabled && power.bus_mv < POWER_OUTPUT_DISABLE_MV)
    power.output_enabled = false;

  HAL_GPIO_WritePin(CTRL_DC_OUTPUT_GPIO_Port, CTRL_DC_OUTPUT_Pin,
                    power.output_enabled ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void UpdateIndicators(uint32_t now)
{
  uint32_t sc_percent;
  uint32_t blink_period;
  uint32_t breath;
  bool system_on;
  bool sc_led_on;

  if (power.faults != 0U)
  {
    system_on = ((now / 250U) & 1U) == 0U;
  }
  else
  {
    breath = (now / 100U) % 20U;
    breath = (breath <= 10U) ? breath : (20U - breath);
    system_on = ((now / POWER_TASK_PERIOD_MS) % 10U) < breath;
  }
  SetLed(SYS_LED_GPIO_Port, SYS_LED_Pin, system_on);
  SetLed(INPUT_LED_GPIO_Port, INPUT_LED_Pin, power.input_valid);

  if (power.sc_mv >= POWER_SC_CHARGE_COMPLETE_MV)
  {
    sc_led_on = true;
  }
  else if (power.sc_mv <= POWER_SC_BACKUP_CUTOFF_MV)
  {
    sc_led_on = false;
  }
  else
  {
    sc_percent = ((power.sc_mv - POWER_SC_BACKUP_CUTOFF_MV) * 100U) /
                 (POWER_SC_CHARGE_COMPLETE_MV - POWER_SC_BACKUP_CUTOFF_MV);
    blink_period = 1200U - (sc_percent * 9U);
    sc_led_on = (now % blink_period) < (blink_period / 2U);
  }
  SetLed(SC_LED_GPIO_Port, SC_LED_Pin, sc_led_on);

  HAL_GPIO_WritePin(STATUS_INPUT_GPIO_Port, STATUS_INPUT_Pin,
                    power.input_valid ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATUS_SC_READY_GPIO_Port, STATUS_SC_READY_Pin,
                    power.sc_mv >= POWER_SC_MANUAL_TEST_STOP_MV ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATUS_BACKUP_GPIO_Port, STATUS_BACKUP_Pin,
                    (power.mode == POWER_MODE_BACKUP || power.mode == POWER_MODE_MANUAL_TEST) ?
                    GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATUS_FAULT_GPIO_Port, STATUS_FAULT_Pin,
                    power.faults != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
  HAL_GPIO_WritePin(STATUS_OUTPUT_GPIO_Port, STATUS_OUTPUT_Pin,
                    (power.output_enabled && !power.output_low && !power.output_high) ?
                    GPIO_PIN_SET : GPIO_PIN_RESET);
}

static const char *ModeName(PowerMode mode)
{
  switch (mode)
  {
    case POWER_MODE_NORMAL: return "NORMAL";
    case POWER_MODE_BACKUP: return "BACKUP";
    case POWER_MODE_MANUAL_TEST: return "MANUAL_TEST";
    case POWER_MODE_TRANSFER_TO_MAIN: return "TRANSFER";
    default: return "UNKNOWN";
  }
}

static void PrintTelemetry(uint32_t now)
{
  char line[224];
  int length;

  if ((now - power.telemetry_at) < POWER_TELEMETRY_PERIOD_MS)
    return;
  power.telemetry_at = now;
  length = snprintf(line, sizeof(line),
                    "SPM input=%lu.%03luV bus=%lu.%03luV sc=%lu.%03luV mode=%s "
                    "charge=%u output=%u faults=0x%02lX muted=%u counts=%lu/%lu\r\n",
                    (unsigned long)(power.input_mv / 1000U), (unsigned long)(power.input_mv % 1000U),
                    (unsigned long)(power.bus_mv / 1000U), (unsigned long)(power.bus_mv % 1000U),
                    (unsigned long)(power.sc_mv / 1000U), (unsigned long)(power.sc_mv % 1000U),
                    ModeName(power.mode), power.charge_enabled ? 1U : 0U,
                    power.output_enabled ? 1U : 0U, (unsigned long)power.faults,
                    power.alarm_muted ? 1U : 0U, (unsigned long)power.record.fault_count,
                    (unsigned long)power.record.backup_count);
  if (length > 0)
  {
    if ((size_t)length >= sizeof(line))
      length = (int)sizeof(line) - 1;
    (void)CDC_Transmit_FS((uint8_t *)line, (uint16_t)length);
  }
}

void PowerControl_Init(ADC_HandleTypeDef *hadc, I2C_HandleTypeDef *hi2c)
{
  uint32_t now = osKernelGetTickCount();

  memset(&power, 0, sizeof(power));
  power.hadc = hadc;
  power.hi2c = hi2c;
  power.mode = POWER_MODE_NORMAL;
  power.mute_button = (Button){BTN1_GPIO_Port, BTN1_Pin, GPIO_PIN_SET, GPIO_PIN_SET, now};
  power.test_button = (Button){BTN2_GPIO_Port, BTN2_Pin, GPIO_PIN_SET, GPIO_PIN_SET, now};
  power.charge_button = (Button){BTN3_GPIO_Port, BTN3_Pin, GPIO_PIN_SET, GPIO_PIN_SET, now};

  HAL_GPIO_WritePin(CTRL_DC_INPUT_GPIO_Port, CTRL_DC_INPUT_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(CTRL_DC_OUTPUT_GPIO_Port, CTRL_DC_OUTPUT_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SC_CHARGE_EN_GPIO_Port, SC_CHARGE_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(SC_OUTPUT_EN_GPIO_Port, SC_OUTPUT_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(BEEP_GPIO_Port, BEEP_Pin, GPIO_PIN_RESET);
  RecordLoad();
}

void PowerControl_Run(void)
{
  uint32_t now = osKernelGetTickCount();
  bool mute_pressed;
  bool test_pressed;
  bool charge_pressed;

  UpdateMeasurements(now);
  mute_pressed = ButtonPressed(&power.mute_button, now);
  test_pressed = ButtonPressed(&power.test_button, now);
  charge_pressed = ButtonPressed(&power.charge_button, now);
  UpdateMode(now, test_pressed);
  UpdateCharge(now, charge_pressed);
  UpdateOutputEnable();
  UpdateFaults(now, mute_pressed);
  UpdateIndicators(now);
  PrintTelemetry(now);
}
