#include "led_control.h"

#include "main.h"
#include "power_config.h"

typedef enum
{
  LED_PATTERN_OFF = 0,
  LED_PATTERN_ON,
  LED_PATTERN_BREATHE,
  LED_PATTERN_ENERGY_BREATHE,
  LED_PATTERN_BLINK
} LedPattern;

typedef struct
{
  volatile uint32_t pattern;
  volatile uint32_t period_ms;
  volatile uint32_t on_ms;
  volatile uint32_t level_percent;
} LedCommand;

static LedCommand system_led;
static LedCommand led5;
static LedCommand sc_led;
static volatile uint32_t led_time_ms;
static uint32_t timer_subtick;
static uint32_t pwm_phase;

static void SetCommand(LedCommand *command, LedPattern pattern,
                       uint32_t period_ms, uint32_t on_ms,
                       uint32_t level_percent)
{
  command->period_ms = period_ms;
  command->on_ms = on_ms;
  command->level_percent = level_percent;
  command->pattern = (uint32_t)pattern;
}

static void WriteActiveLow(GPIO_TypeDef *port, uint16_t pin, bool on)
{
  if (on)
    port->BSRR = (uint32_t)pin << 16U;
  else
    port->BSRR = pin;
}

static bool Render(const LedCommand *command)
{
  uint32_t pattern = command->pattern;
  uint32_t phase_ms;
  uint32_t level;
  uint32_t duty;

  if (pattern == (uint32_t)LED_PATTERN_ON)
    return true;
  if (pattern == (uint32_t)LED_PATTERN_OFF)
    return false;
  if (pattern == (uint32_t)LED_PATTERN_BLINK)
  {
    if (command->period_ms == 0U)
      return false;
    return (led_time_ms % command->period_ms) < command->on_ms;
  }

  phase_ms = led_time_ms % POWER_SYS_BREATHE_PERIOD_MS;
  if (phase_ms <= (POWER_SYS_BREATHE_PERIOD_MS / 2U))
    level = (phase_ms * POWER_LED_PWM_STEPS) / (POWER_SYS_BREATHE_PERIOD_MS / 2U);
  else
    level = ((POWER_SYS_BREATHE_PERIOD_MS - phase_ms) * POWER_LED_PWM_STEPS) /
            (POWER_SYS_BREATHE_PERIOD_MS / 2U);

  /* Quadratic gamma correction gives a visually smooth brightness ramp. */
  duty = (level * level + (POWER_LED_PWM_STEPS / 2U)) / POWER_LED_PWM_STEPS;
  if (pattern == (uint32_t)LED_PATTERN_ENERGY_BREATHE)
    duty = (duty * command->level_percent + 50U) / 100U;
  return pwm_phase < duty;
}

void LedControl_Init(TIM_HandleTypeDef *htim)
{
  led_time_ms = 0U;
  timer_subtick = 0U;
  pwm_phase = 0U;
  SetCommand(&system_led, LED_PATTERN_OFF, 0U, 0U, 0U);
  SetCommand(&led5, LED_PATTERN_OFF, 0U, 0U, 0U);
  SetCommand(&sc_led, LED_PATTERN_OFF, 0U, 0U, 0U);
  WriteActiveLow(SYS_LED_GPIO_Port, SYS_LED_Pin, false);
  WriteActiveLow(INPUT_LED_GPIO_Port, INPUT_LED_Pin, false);
  WriteActiveLow(SC_LED_GPIO_Port, SC_LED_Pin, false);
  if (HAL_TIM_Base_Start_IT(htim) != HAL_OK)
    Error_Handler();
}

void LedControl_SetSystem(bool fault_active)
{
  if (fault_active)
    SetCommand(&system_led, LED_PATTERN_BLINK, POWER_FAULT_BLINK_PERIOD_MS,
               POWER_FAULT_BLINK_PERIOD_MS / 2U, 100U);
  else
    SetCommand(&system_led, LED_PATTERN_BREATHE, POWER_SYS_BREATHE_PERIOD_MS,
               0U, 100U);
}

void LedControl_SetLed5(bool input_valid, bool charging, bool manual_test)
{
  if (!input_valid)
    SetCommand(&led5, LED_PATTERN_OFF, 0U, 0U, 0U);
  else if (manual_test)
    SetCommand(&led5, LED_PATTERN_BLINK, POWER_LED5_PATTERN_PERIOD_MS,
               POWER_LED5_PATTERN_PERIOD_MS / 5U, 100U);
  else if (charging)
    SetCommand(&led5, LED_PATTERN_BLINK, POWER_LED5_PATTERN_PERIOD_MS,
               POWER_LED5_PATTERN_PERIOD_MS / 2U, 100U);
  else
    SetCommand(&led5, LED_PATTERN_ON, 0U, 0U, 100U);
}

void LedControl_SetSupercapEnergy(uint32_t energy_percent)
{
  if (energy_percent == 0U)
    SetCommand(&sc_led, LED_PATTERN_OFF, 0U, 0U, 0U);
  else
  {
    if (energy_percent > 100U)
      energy_percent = 100U;
    SetCommand(&sc_led, LED_PATTERN_ENERGY_BREATHE,
               POWER_SYS_BREATHE_PERIOD_MS, 0U, energy_percent);
  }
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance != TIM2)
    return;

  ++pwm_phase;
  if (pwm_phase >= POWER_LED_PWM_STEPS)
    pwm_phase = 0U;

  ++timer_subtick;
  if (timer_subtick >= (POWER_LED_TIMER_FREQUENCY_HZ / 1000U))
  {
    timer_subtick = 0U;
    ++led_time_ms;
  }

  WriteActiveLow(SYS_LED_GPIO_Port, SYS_LED_Pin, Render(&system_led));
  WriteActiveLow(INPUT_LED_GPIO_Port, INPUT_LED_Pin, Render(&led5));
  WriteActiveLow(SC_LED_GPIO_Port, SC_LED_Pin, Render(&sc_led));
}
