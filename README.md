# GMP 超级电容辅助供电控制器

本工程基于 STM32F411CEU6，实现外部 12 V 输入监测、超级电容充电、12 V
BOOST 备电、用户输出控制、故障报警、EEPROM 事件计数和 USB CDC 调试输出。
外设配置以 STM32F411CEU6_SPM/STM32F411CEU6_SPM.ioc 为准。

## 上电流程

1. CTRL_DC_OUTPUT=0，首先断开用户输出。
2. 允许外部输入并启动超级电容充电；充电和 BOOST 硬件互斥。
3. SC_BUS_ADC 达到 10.5 V 后，LED4 进入 100% 峰值亮度呼吸，并继续保持
   充电 5 s。
4. 关闭充电器，启动 BOOST，等待 200 ms 使 12 V 母线稳定。
5. 当 DC_BUS_ADC 满足输出条件后，接通用户输出。

进入正常状态后，BOOST 默认始终运行，以避免外部输入消失时等待 BOOST
启动而产生明显的母线跌落。仅在超级电容充电或用户强制关闭 BOOST 时停止。

## 按键功能

三个按键均由外部电阻上拉，按下为低电平。

| 按键 | MCU | 单独按下 | 按住 K1 后按下 |
| --- | --- | --- | --- |
| K1 / BTN1 | PB5 | 当前报警已经触发时关闭蜂鸣器；没有报警时不起作用 | 作为组合键修饰键 |
| K2 / BTN2 | PB4 | 进入或退出强制 SC 供电测试；SC 低于 5.0 V 自动退出 | 切换 boost_forced_off，强制关闭/恢复 BOOST |
| K3 / BTN3 | PA15 | 启动充电或正常模式下切换超级电容充电请求 | 切换 output_forced_off，强制关闭/恢复用户输出 |

组合键操作方法：先按住 K1，短按一次 K2 或 K3，再松开 K1。组合功能是
锁存式切换，再次执行同一组合键即可恢复。启动充电阶段会忽略 K2 的普通
单键操作，但允许单独按 K3 暂停或恢复首次充电；组合键仍可用于安全强制
控制。

首次充电阶段按 K3 暂停充电后，系统保持在 STARTUP_CHARGE，用户输出继续
断开，充满确认计时清零。再次按 K3 恢复充电，SC 达到 10.5 V 并连续保持
5 s 后，系统才进入 BOOST 建立和用户输出启动流程。

新的报警条件出现时会自动取消之前的静音，蜂鸣器重新报警。

## LED 状态

三只 LED 均为低电平点亮。TIM2 产生 10 kHz 中断，并以 200 Hz、50 级
软件 PWM 驱动 LED；系统呼吸灯使用二次伽马修正，避免原来肉眼可见的闪烁。

### LED3：系统灯（PC13）

| 状态 | 显示 |
| --- | --- |
| 系统无故障 | 2 s 周期平滑呼吸 |
| 任意故障有效 | 500 ms 周期、50% 占空比闪烁 |

### LED4：超级电容电量灯（PB13）

| 可用能量 | 显示 |
| --- | --- |
| 0% | 熄灭 |
| 1%～99% | 2 s 周期平滑呼吸，呼吸峰值亮度等于能量百分比 |
| 100% | 2 s 周期、100% 峰值亮度呼吸 |

电容储能满足 E=1/2×C×V²，因此剩余能量不能按电压线性估算。程序使用以下
可用能量百分比：

    energy_percent =
        (Vsc² - Vcutoff²) / (Vfull² - Vcutoff²) × 100%

其中 Vcutoff=4.5 V，Vfull=10.5 V。低于截止电压的能量不计入可用能量。
例如 energy_percent=20% 时，呼吸峰值 PWM 亮度为 20%；80% 时峰值亮度为
80%。呼吸周期保持不变，只通过整体亮度反映实际可用储能。

## 蜂鸣器模式

蜂鸣器会根据供电阶段切换提示方式：

| buzzer_mode | 条件 | 声音 |
| ---: | --- | --- |
| 0 | 无报警、报警已静音或用户输出已经切断 | 静音 |
| 1 | 外部输入丢失或母线异常，且用户输出仍在供电 | 连续报警 |
| 2 | BACKUP/MANUAL_TEST 中可用能量低于或等于 10% | 三次短促“嘀嘀嘀”循环 |

低能量短促报警的一个周期为 1.2 s：响 100 ms、停 100 ms，重复三次，然后
静音 600 ms。进入低能量预警会作为新的报警阶段，自动取消之前对输入丢失
报警的静音；此时仍可再次按 K1 静音。

按当前 4.5 V 截止、10.5 V 满电参数计算，10% 可用能量大约对应 SC 电压
5.41 V；修改满电或截止电压后，该电压点会自动随能量公式变化。

当 output_enabled 变为 0、用户输出已经切断后，蜂鸣器无条件关闭，不再因为
母线继续下降而报警。

### LED5：外部输入及工作状态灯（PB12）

判断优先级从上到下：

| 状态 | 显示 |
| --- | --- |
| 外部输入无效 | 熄灭 |
| 强制 SC 供电测试 | 1 s 周期、20% 占空比闪烁 |
| 超级电容正在充电 | 1 s 周期、50% 占空比闪烁 |
| 外部输入有效、无上述特殊状态 | 常亮 |

## 电源状态机

调试变量 g_power_debug.mode 的取值如下：

| 数值 | 状态 | 说明 |
| ---: | --- | --- |
| 0 | STARTUP_CHARGE | 用户输出关闭，等待 SC 达到 10.5 V 并保持 5 s |
| 1 | STARTUP_BOOST_SETTLE | 充电已关闭，BOOST 已启动，等待母线稳定 |
| 2 | NORMAL | 外部输入有效，BOOST 默认保持运行 |
| 3 | BACKUP | 外部输入丢失，由超级电容和 BOOST 供电 |
| 4 | MANUAL_TEST | K2 触发的强制 SC 供电测试 |

充电器和 BOOST 始终互斥：

- charge_enabled=1 时一定有 boost_enabled=0。
- charge_enabled=0 且没有强制关闭时，正常运行期间 boost_enabled=1。
- 输入在充电期间快速跌落到 9.0 V 以下时，会绕过普通消抖，立即关闭充电并
  启动 BOOST。

### 状态迁移条件

| 当前状态 | 触发条件 | 下一状态 | 主要动作 |
| --- | --- | --- | --- |
| STARTUP_CHARGE | 单独按 K3 | STARTUP_CHARGE | 暂停或恢复首次充电，用户输出保持断开 |
| STARTUP_CHARGE | SC 连续高于或等于 10.5 V 达 5 s | STARTUP_BOOST_SETTLE | 关闭充电器、启动 BOOST |
| STARTUP_BOOST_SETTLE | BOOST 已运行 200 ms | NORMAL 或 BACKUP | 设置 startup_complete，允许用户输出 |
| NORMAL | 外部输入无效 | BACKUP | 关闭主输入路径，BOOST 已经处于运行状态 |
| NORMAL | 单独按 K2 且 SC 高于 5.0 V | MANUAL_TEST | 关闭主输入路径，强制使用 SC |
| NORMAL | 单独按 K3 | NORMAL | 只切换充电请求，不改变状态机模式 |
| BACKUP | 外部输入恢复 | NORMAL | 重新允许主输入，BOOST 继续运行 |
| MANUAL_TEST | 再次单独按 K2 | NORMAL | 退出测试并恢复主输入 |
| MANUAL_TEST | SC 低于 5.0 V | NORMAL 或 BACKUP | 根据外部输入是否有效决定去向 |
| MANUAL_TEST | 外部输入物理丢失 | BACKUP | 转为真实掉电备电 |

### 各状态下的控制输出

表中 1 表示控制引脚高电平，0 表示低电平；“自动”表示还要满足电压和强制
锁存条件。

| 状态 | CTRL_DC_INPUT | SC_CHARGE_EN | SC_OUTPUT_EN | CTRL_DC_OUTPUT |
| --- | ---: | ---: | ---: | ---: |
| STARTUP_CHARGE，输入有效且未充满 | 1 | 1 | 0 | 0 |
| STARTUP_BOOST_SETTLE | 1 | 0 | 1 | 0 |
| NORMAL，未充电 | 1 | 0 | 1 | 自动，通常为 1 |
| NORMAL，正在充电 | 1 | 1 | 0 | 保持原有输出 |
| BACKUP | 0 | 0 | 1 | 自动，母线过低时关闭 |
| MANUAL_TEST | 0 | 0 | 1 | 自动，母线过低时关闭 |

例外情况：

- boost_forced_off=1 时，SC_OUTPUT_EN 强制为 0。
- output_forced_off=1 时，CTRL_DC_OUTPUT 强制为 0。
- BACKUP 或 MANUAL_TEST 中 SC 低于 4.5 V 时，BOOST 会关闭以防止超级
  电容继续深度放电。
- SC_CHARGE_EN 和 SC_OUTPUT_EN 在任何情况下都不会同时为 1。

## Keil 调试建议

在 Watch 窗口添加全局变量 g_power_debug。该变量专门提供稳定、易观察的
调试快照：

| 成员 | 含义 |
| --- | --- |
| input_mv | DC_BUS_IN_ADC 换算后的外部输入电压，单位 mV |
| bus_mv | DC_BUS_ADC 换算后的内部 12 V 母线电压，单位 mV |
| sc_mv | SC_BUS_ADC 换算后的超级电容组电压，单位 mV |
| sc_energy_percent | 按电压平方计算的可用能量百分比，范围 0～100 |
| mode | 当前状态机状态，取值见上表 |
| input_valid | 外部 12 V 输入是否有效 |
| boost_enabled | BOOST 控制输出是否实际使能 |
| charge_requested | K3 锁存的充电请求；首次充电阶段也可切换 |
| charge_enabled | 充电控制输出是否实际使能 |
| output_enabled | 用户输出控制是否实际使能 |
| boost_forced_off | K1+K2 强制关闭 BOOST 的锁存状态 |
| output_forced_off | K1+K3 强制关闭用户输出的锁存状态 |
| startup_complete | 上电充电和 BOOST 稳定流程是否已经完成 |
| alarm_muted | 当前报警是否被 K1 静音 |
| low_energy_warning | 可用能量是否已经进入 10% 低能量预警区 |
| buzzer_mode | 蜂鸣器模式：0 静音、1 连续报警、2 三次短鸣 |
| faults | 当前故障位掩码 |

建议重点检查以下组合：

- 正常运行：mode=2、input_valid=1、boost_enabled=1、
  charge_enabled=0、output_enabled=1。
- 启动充电：mode=0、charge_enabled=1、boost_enabled=0、
  output_enabled=0。
- 自动备电：mode=3、input_valid=0、boost_enabled=1。
- 手动 SC 测试：mode=4、boost_enabled=1，LED5 为 20% 闪烁。
- 强制关闭 BOOST：boost_forced_off=1、boost_enabled=0。
- 强制关闭输出：output_forced_off=1、output_enabled=0。

故障位定义：

| 位 | 数值 | 含义 |
| ---: | ---: | --- |
| 0 | 0x01 | 外部输入丢失 |
| 1 | 0x02 | 12 V 母线欠压 |
| 2 | 0x04 | 12 V 母线过压 |
| 3 | 0x08 | 备电期间超级电容耗尽 |
| 4 | 0x10 | 母线在检测窗口内快速跌落 |
| 5 | 0x20 | ADC 采样失败 |

USB CDC 每 2 s 输出同样的信息，包括三路电压、状态机模式、BOOST、充电器、
用户输出、两个强制锁存状态、故障位以及 EEPROM 中的累计故障/备电次数。

### CDC 输出字段

典型输出如下：

    SPM input=12.031V bus=11.982V sc=10.512V energy=100% mode=NORMAL
    boost=1 charge_req=0 charge=0 output=1 force_boost_off=0 force_output_off=0
    faults=0x00 buzzer=0 muted=0 counts=2/1

实际输出为一行，上面为了阅读进行了换行。字段含义：

| 字段 | 说明 |
| --- | --- |
| input | 外部 12 V 输入电压 |
| bus | 内部供电母线电压 |
| sc | 超级电容组电压 |
| energy | 以 4.5 V 为可用能量零点、按 V² 计算的剩余能量百分比 |
| mode | 当前状态机名称 |
| boost | BOOST 实际使能状态 |
| charge_req | K3 锁存的充电请求状态 |
| charge | 充电器实际使能状态 |
| output | 用户输出实际使能状态 |
| force_boost_off | K1+K2 强制关闭 BOOST 的锁存状态 |
| force_output_off | K1+K3 强制关闭用户输出的锁存状态 |
| faults | 当前故障位掩码 |
| buzzer | 蜂鸣器模式：0 静音、1 连续报警、2 三次短鸣 |
| muted | 蜂鸣器是否被当前故障周期静音 |
| counts | EEPROM 保存的累计故障次数/自动备电次数 |

## 硬件控制信号速查

| 功能 | MCU | 有效电平 | 说明 |
| --- | --- | --- | --- |
| SC_CHARGE_EN | PB2 | 高 | 开启超级电容充电器 |
| SC_OUTPUT_EN | PB14 | 高 | 开启超级电容 BOOST |
| CTRL_DC_INPUT | PB9 | 高 | 允许外部 12 V 电源进入内部母线 |
| CTRL_DC_OUTPUT | PC15 | 高 | 接通用户负载输出 |
| BEEP | PB8 | 高 | 蜂鸣器报警 |
| BTN1 / K1 | PB5 | 低 | 报警静音/组合键修饰 |
| BTN2 / K2 | PB4 | 低 | SC 测试/BOOST 强制控制 |
| BTN3 / K3 | PA15 | 低 | 充电请求/用户输出强制控制 |
| LED3 | PC13 | 低 | 系统状态灯 |
| LED4 | PB13 | 低 | SC 电量灯 |
| LED5 | PB12 | 低 | 外部输入和运行状态灯 |

## 外部状态 GPIO

以下信号均为高电平有效：

| 原理图信号 | MCU | 含义 |
| --- | --- | --- |
| STM32_GPIO1 | PA7 | 外部输入有效 |
| STM32_GPIO2 | PA5 | SC 电压高于手动测试下限 |
| STM32_GPIO3 | PA6 | 自动备电或手动 SC 测试有效 |
| STM32_GPIO4 | PA4 | 任意故障有效 |
| STM32_GPIO5 | PB0 | 用户输出已开启且母线电压正常 |

## 推荐联调与验收流程

调试时建议同时观察示波器波形、Keil 中的 g_power_debug 和 USB CDC 输出。

### 1. 冷启动与预充电

1. 将 SC 放电到低于 10.5 V，再接入外部 12 V。
2. 确认 mode=0、charge_enabled=1、boost_enabled=0、
   output_enabled=0。
3. 确认 LED5 为 1 s/50% 闪烁，用户输出端保持断开。
4. 单独按 K3，确认 charge_enabled=0、mode 和 output_enabled 不变；
   再次按 K3 后确认 charge_enabled 恢复为 1。
5. SC 达到 10.5 V 时确认 LED4 达到 100% 峰值亮度呼吸。
6. 从首次达到 10.5 V 开始计时，连续 5 s 后充电器应关闭。

如果保持期间 SC 重新低于 10.5 V，5 s 计时会清零并重新开始。
充电过程中 LED4 的呼吸周期应保持约 2 s；随着 SC 储能增加，呼吸峰值亮度
逐渐提高。亮度变化应符合电压平方关系，而不是电压线性关系。

### 2. BOOST 建立和用户输出启动

1. 观察 mode 从 0 变为 1。
2. 确认 charge_enabled 从 1 变为 0 后，boost_enabled 才变为 1。
3. 等待约 200 ms 后，startup_complete 应变为 1。
4. 母线满足条件时 output_enabled 变为 1，用户输出得到 12 V。
5. 正常状态下确认 mode=2、boost_enabled=1。

### 3. 自动掉电备电

1. 正常运行且不充电时断开外部输入。
2. 用示波器同时观察 DC_BUS_IN、DC_BUS_12V 和用户输出 12 V。
3. 确认 BOOST 在断电前已经开启，DC_BUS_12V 不应出现导致主处理器复位的
   启动空窗。
4. 确认 mode=3、input_valid=0、boost_enabled=1，LED5 熄灭。
5. 当 sc_energy_percent 降到 10% 时，确认 buzzer_mode=2，蜂鸣器发出
   三次短促提示。
6. 当 output_enabled 降到 0 后，确认 buzzer_mode=0 且蜂鸣器停止。
7. 恢复外部输入后，确认系统回到 mode=2，BOOST 仍保持开启。

### 4. 充电期间的掉电切换

1. 在 NORMAL 状态单独按 K3，确认 charge_enabled=1、
   boost_enabled=0。
2. 此时断开外部输入。
3. 输入原始采样低于 9.0 V 后，应立即关闭充电器并启动 BOOST。
4. 用示波器检查该工况下的最低母线电压；这是相对于正常待机掉电更苛刻的
   测试条件。

### 5. 手动 SC 供电测试

1. 保证 SC 高于 5.0 V，单独按 K2。
2. 确认 mode=4、CTRL_DC_INPUT=0、boost_enabled=1。
3. 确认 LED5 为 1 s/20% 闪烁。
4. 再次按 K2 应退出测试；若 SC 降到 5.0 V 以下也应自动退出。

### 6. 组合键测试

1. 按住 K1 后短按 K2，确认 boost_forced_off 和 boost_enabled 互为相反
   状态；再次执行应恢复。
2. 按住 K1 后短按 K3，确认 output_forced_off=1 且
   output_enabled=0；再次执行应恢复。
3. 组合键测试时要先等待 K1 消抖完成，再按 K2/K3，避免被识别为普通单键。

### 7. 互锁和报警检查

1. 示波器同时连接 PB2 和 PB14，覆盖启动、手动充电、掉电、恢复等工况。
2. 验收要求：PB2 和 PB14 不得同时为高电平。
3. 输入丢失或母线欠压时，确认 BEEP=1；按 K1 后仅当前故障周期静音。
4. 备电能量降到 10% 时，确认静音被重新解除并切换为三次短鸣；再次按 K1
   可以关闭该阶段报警。
5. 用户输出切断后，确认即使 faults 仍非零，BEEP 也保持为 0。
6. 故障恢复后再次制造故障，蜂鸣器必须重新报警。
7. 对照 faults 检查故障位，并确认 EEPROM counts 在新故障或自动备电事件后
   增加。

## LED 调试注意事项

- TIM2 输入时钟为 96 MHz，PSC=95、ARR=99，对应 10 kHz 更新中断。
- 呼吸灯软件 PWM 载波为 200 Hz，共 50 个占空比等级。
- LED4 使用 2 s 呼吸周期，峰值亮度由电压平方计算得到的可用能量百分比决定。
- LED 输出由 TIM2 中断维护，任务中只更新显示模式。因此单步调试或全局关
  中断时，LED 会暂停在当时状态，这不代表状态机停止工作。
- 若 LED 仍有肉眼可见闪烁，应先确认 TIM2_IRQHandler 持续进入、
  HAL_TIM_PeriodElapsedCallback 未被其他文件重复定义，并检查系统是否实际
  处于 faults 非零的故障闪烁状态。

## 主要可调参数

参数集中在 STM32F411CEU6_SPM/Core/Inc/power_config.h，包括：

- 外部输入和 12 V 母线欠压/过压阈值。
- SC 充满阈值 10.5 V、保持充电时间 5 s。
- 手动测试停止阈值 5.0 V、备电截止阈值 4.5 V。
- 低能量短鸣预警阈值，默认可用能量为 10%。
- BOOST 启动稳定时间 200 ms。
- CDC 输出周期和 LED 定时参数。

## Keil 与 CubeMX

当前 .ioc 目标工具链为 MDK-ARM V5.32，并已配置 TIM2、ADC1、I2C1、
FreeRTOS 和 USB CDC。CubeMX 重新生成后应确认以下自定义文件仍在
Application/User/Core 组内：

- Core/Src/power_control.c
- Core/Src/led_control.c

GCC 的 syscalls.c 和 sysmem.c 不应加入 Keil 工程。

CubeMX 再生成后的核对清单：

1. TIM2 保持 PSC=95、Counter Period=99，并开启全局中断。
2. Core/Src/led_control.c 和 Core/Src/power_control.c 位于 Keil 工程中。
3. main.c 的用户代码区仍调用 PowerControl_Init，并传入 hadc1、hi2c1、
   htim2。
4. Keil 工程中不存在重复的 system_stm32f4xx.c。
5. Keil 工程中不编译 GCC 专用的 syscalls.c 和 sysmem.c。
