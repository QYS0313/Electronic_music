# Electronic Music — STM32 电子琴

基于 STM32G431CBT6 的便携式电子琴。固件内置一个 4 声部 FM 合成器，19 个实体按键，通过片上 12 位 DAC 输出音频。仓库同时包含固件、PCB 工程和外壳 3D 模型，是一套完整的开源硬件项目。

## 功能特性

- **4 声部复音**：可同时按下 4 个键，超出时自动替换最早按下的声部
- **12 种音色**：钢琴、木琴、笛音等，通过专用按键循环切换
- **FM 合成 + ADSR 包络**：每种音色有独立的起音/衰减/延音/释放参数和 FM 调制参数
- **48 kHz 采样率**：由 TIM6 定时中断驱动，DDS 相位累加查表生成波形
- **18 个音键**：覆盖 C3 起的音域，不同音色有各自的基准音高偏移
- **纯硬件按键扫描**：GPIO 上拉输入，5 ms 轮询去抖

## 仓库结构

```
Electronic_music/
├── firmware/              STM32 固件（STM32CubeMX + CMake 工程）
│   ├── Core/              应用代码，合成器实现在 Core/Src/main.c
│   ├── Drivers/           STM32G4 HAL 库与 CMSIS
│   ├── Electronic_music.ioc   CubeMX 配置文件
│   └── CMakeLists.txt
├── hardware/              PCB 设计
│   ├── Electronic_music.epro2       嘉立创 EDA 专业版工程（原理图 + PCB）
│   └── Electronic_music_Gerber.zip  Gerber 生产文件，可直接用于打板
└── mechanical/            外壳 3D 模型
    ├── solidworks/        SolidWorks 源文件（4 个零件 + 总装配体）
    └── stl/               导出的 STL，可直接 3D 打印
```

## 硬件

| 项目 | 说明 |
|------|------|
| 主控 | STM32G431CBT6，Cortex-M4F，170 MHz |
| 音频输出 | DAC1 通道 1（PA4），12 位 |
| 采样定时 | TIM6，170 MHz / 17 / 208 ≈ 48 kHz |
| 按键 | 19 个 GPIO 输入（内部上拉，按下接地） |
| 调试接口 | SWD（PA13 / PA14） |

按键与引脚的对应关系见 `firmware/Core/Src/main.c` 中的 `keymap` 数组，第 18 号键为音色切换键。

DAC 输出为 0～3.3 V 的直流偏置信号，需经过外部低通滤波和功放后驱动扬声器，电路见 PCB 工程。

## 固件编译

### 环境要求

- [Arm GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)（arm-none-eabi-gcc）
- CMake 3.22 以上
- Ninja
- STM32CubeMX（仅在需要修改外设配置时）

### 编译步骤

```bash
cd firmware
cmake --preset Debug
cmake --build --preset Debug
```

生成的 `Electronic_music.elf` 位于 `firmware/build/Debug/`，可用 STM32CubeProgrammer 或 OpenOCD 通过 SWD 烧录。

也可以直接用 VS Code 打开 `firmware` 文件夹，安装 STM32 VS Code Extension 后一键编译下载。

## 合成器原理简述

1. **音高**：启动时预计算 48 个半音（C3～B6）的 32 位相位增量，按键时查表得到载波频率。
2. **FM 调制**：每个声部有一个调制器相位累加器，调制深度随时间按音色参数指数衰减，形成不同的音色质感。
3. **ADSR 包络**：每约 0.5 ms 更新一次，按音色参数控制音量变化。
4. **混音输出**：4 个声部叠加后乘以增益、限幅，映射到 0～4095 写入 DAC。

## 制作

1. 用 `hardware/Electronic_music_Gerber.zip` 打板并焊接元件
2. 3D 打印 `mechanical/stl/` 下的全部零件
3. 编译烧录固件
4. 组装

## 许可

代码部分基于 STMicroelectronics HAL 库，遵循其原有许可。其余部分由作者保留权利，欢迎学习交流。
