# GMP Supercapacitor Power Module

STM32F411-based 12 V auxiliary-power controller with automatic supercapacitor
backup, controlled charging, manual backup testing, fault annunciation and USB
CDC telemetry.

## User controls and indicators

- `BTN1` (PB5): mute the buzzer for the currently active alarm. It has no effect
  before an alarm occurs, and a new alarm condition re-enables the buzzer.
- `BTN2` (PB4): enter or leave manual supercapacitor test mode. The test ends
  automatically at the configured supercapacitor voltage limit.
- `BTN3` (PA15): toggle the supercapacitor charge request. Charging stops after
  the voltage reaches the completion threshold and the hold time expires.
- System LED (PC13): breathing when healthy, flashing when a fault is active.
- Input LED (PB12): on while the external 12 V source is valid.
- Supercapacitor LED (PB13): off when depleted, variable-rate flashing while
  charged, and continuously on at the charge-complete voltage.

All three LEDs are active-low. The push buttons are active-low and have external
pull-ups. The buzzer is active-high.

## External status GPIOs

| Schematic signal | MCU pin | Meaning (active-high) |
| --- | --- | --- |
| STM32_GPIO1 | PA7 | External input valid |
| STM32_GPIO2 | PA5 | Supercapacitor above the manual-test threshold |
| STM32_GPIO3 | PA6 | Backup or manual-test mode active |
| STM32_GPIO4 | PA4 | Any controller fault active |
| STM32_GPIO5 | PB0 | Load output enabled and bus voltage valid |

## Configuration and telemetry

Voltage thresholds, ADC divider values and timing parameters are centralized in
`STM32F411CEU6_SPM/Core/Inc/power_config.h`. The default manual-test stop voltage
is 5.0 V, charge completion is 10.5 V, charge hold is 30 s, and CDC telemetry is
sent every 2 s.

The CDC line reports the three measured voltages, operating mode, charge/output
states, fault mask, mute state and persistent EEPROM fault/backup counters.
Fault bits are: `0x01` input lost, `0x02` output undervoltage, `0x04` output
overvoltage, `0x08` supercapacitor depleted, `0x10` rapid bus drop and `0x20` ADC
failure.

## Build

```text
cd STM32F411CEU6_SPM
cmake --preset Debug
cmake --build --preset Debug --parallel
```

The `.ioc` file remains the source of truth for CubeMX peripheral management.
The checked CMake project uses the GCC FreeRTOS port. Before generating the final
Keil project, select the MDK-ARM toolchain in CubeMX so it emits the matching
FreeRTOS portable layer, then ensure `Core/Src/power_control.c` remains in the
application source group.
