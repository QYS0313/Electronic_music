/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "dac.h"
#include "stm32g4xx_hal.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "math.h"
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

//合成器参数
// 乐器定义
#define NINSTR 12// 定义乐器数量为12种：钢琴、木琴、笛音等
// 每种乐器的音量值
static const uint16_t ldness[NINSTR] =
  {64,64,64,64,64,64,64,64,64,64,64,64};
// 每种乐器的基准音高
static const uint8_t pitch0[NINSTR] =
  {12,12,12,12,24,24,0,12,24,12,12,24};
// ADSR 包络的起音参数
static const uint16_t ADSR_a[NINSTR] =
  {4096,8192,8192,8192,4096,512,512,8192,128,128,256,256};
// ADSR 包络的衰减参数
static const uint16_t ADSR_d[NINSTR] =
  {8,32,16,16,8,16,16,8,16,16,64,32};
// ADSR 包络的延音参数
static const uint16_t ADSR_s[NINSTR] =
  {0,0,0,0,0,0,0,0,240,240,192,192};
// ADSR 包络的释放参数
static const uint16_t ADSR_r[NINSTR] =
  {64,128,32,32,16,32,32,32,32,32,64,64};
// 每种乐器的 FM 调制频率增量，与音高相关
static const uint16_t FM_inc_tab[NINSTR] =
  {256,512,768,400,200,96,528,244,256,128,64,160};
// FM 调制幅度起始值
static const uint16_t FM_a1_tab[NINSTR] =
  {128,512,512,1024,512,0,1024,2048,256,256,384,256};
// FM 调制幅度结束值
static const uint16_t FM_a2_tab[NINSTR] =
  {64,0,128,128,128,512,768,512,128,128,256,128};
// FM 衰减参数
static const uint16_t FM_dec_tab[NINSTR] =
  {64,128,128,128,32,128,128,128,128,128,64,64};

//键定义
#define KEY_NOKEY 255// 无按键按下
#define KEY_INSTR 254// 乐器选择按键
typedef struct { GPIO_TypeDef* port; uint16_t pin; } KeyPin;
KeyPin keymap[19] = {
  /* fill with your real pins */
  {GPIOA, GPIO_PIN_12}, /* key 0: e.g. C5 -> replace with your pin */
  {GPIOB, GPIO_PIN_4},
  {GPIOB, GPIO_PIN_6},
  {GPIOB, GPIO_PIN_7},
  {GPIOB, GPIO_PIN_9},
  {GPIOB, GPIO_PIN_11},
  {GPIOB, GPIO_PIN_1},
  {GPIOB, GPIO_PIN_0},
  {GPIOA, GPIO_PIN_2},
  {GPIOC, GPIO_PIN_15},
  {GPIOC, GPIO_PIN_13},
  {GPIOB, GPIO_PIN_3},
  {GPIOB, GPIO_PIN_5},
  {GPIOA, GPIO_PIN_15},
  {GPIOB, GPIO_PIN_10},
  {GPIOB, GPIO_PIN_2},
  {GPIOA, GPIO_PIN_7},
  {GPIOA, GPIO_PIN_1},
  {GPIOB, GPIO_PIN_13}
};

//DDS / 波表
#define SINE_LEN 256
static int16_t sine_table[SINE_LEN];

//相位累加器宽度
typedef uint32_t phase_t;

//采样率（与 TIM6 设置匹配）
#define SAMPLE_RATE 48082UL /* ~170MHz/(17*208) */

//48 个半音的预计算音调增量 （C3 ..B6）
#define TONE_COUNT 48
static uint32_t tone_inc[TONE_COUNT]; // 每个半音的每个样本的相位增量

//合成器运行时状态 
#define NCH 4
static phase_t phase_acc[NCH];
static uint32_t inc_base[NCH];
static uint8_t amp_base[NCH];
static uint32_t envADSR[NCH];
static uint8_t iADSR[NCH];
static uint32_t FMphase[NCH];
static int32_t FMinc_base[NCH];
static int32_t FMa0[NCH];
static int32_t FMda[NCH];
static uint32_t FMexp[NCH];
static uint16_t FMdec[NCH];
static uint8_t keych[NCH];
static uint32_t tch[NCH];

static volatile uint8_t current_instr = 0;

//键态简单轮询
#define KEYCOUNT 19
static uint8_t lastKeyStates[KEYCOUNT];






/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

//实用程序：计算半音索引 i 的频率，其中索引 0 = C3 （MIDI 48）
static double midi_freq(int midi_note) {
  return 440.0 * pow(2.0, (midi_note - 69) / 12.0);
}

/* TIM6 中断回调 - 合成器的核心 （48kHz） */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  if (htim->Instance == TIM6) {
    /* ---更新 FM exp 衰减（矢量化）--- */
    for (int ch=0; ch<NCH; ch++){
      /* FMexp 衰减：近似原始行为 */
      FMphase[ch] += FMinc_base[ch];
      phase_acc[ch] += inc_base[ch];
    }


    static uint16_t env_counter = 0;
    const uint16_t ENV_DECIM = 23;
    if (++env_counter >= ENV_DECIM){ 
      env_counter = 0;

      /* FMexp 衰减（慢） */
      for (int ch=0; ch<NCH; ch++){
        FMexp[ch] -= ( (uint64_t)FMexp[ch] * FMdec[ch]) >> 16;
      }


      /* --- ADSR 更新和信道时间计数器 --- */
      for (int ch=0; ch<NCH; ch++){
        switch (iADSR[ch]) {
          case 0: /* off */ break;
          case 1: /*攻击*/
            if ((0xFFFF - envADSR[ch]) <= ADSR_a[current_instr]) {
              envADSR[ch] = 0xFFFF;
              iADSR[ch] = 2;
            } else {
              envADSR[ch] += ADSR_a[current_instr];
            }
            break;
          case 2: /*衰变*/
            if (envADSR[ch] <= (uint32_t)(ADSR_s[current_instr] << 8) + ADSR_d[current_instr]) {
              envADSR[ch] = (ADSR_s[current_instr] << 8);
              iADSR[ch] = 3;
            } else {
              envADSR[ch] -= ADSR_d[current_instr];
            }
            break;
          case 3: /*维持*/ break;
          case 4: /*释放*/
            if (envADSR[ch] <= ADSR_r[current_instr]) {
              envADSR[ch] = 0;
              iADSR[ch] = 0;
              amp_base[ch]=0;
              keych[ch]=KEY_NOKEY;
            } else {
              envADSR[ch] -= ADSR_r[current_instr];
            }
            break;
        }
        tch[ch]++; /* time counter */
      }
    }
     

    /* --- 从 4 个声部计算混合样本 --- */
    int32_t mix = 0;
    for (int ch=0; ch<NCH; ch++){
      if (iADSR[ch] == 0) {
        /* voice off */
        continue;
      }
      /* 高级调频相位 */
      FMphase[ch] += FMinc_base[ch];
      /* 推进主相 */
      phase_acc[ch] += inc_base[ch];

      /* 采样 FM 调制器 */
      uint8_t fm_index = (uint8_t)(FMphase[ch] >> 24); /* top 8 bits */
      int32_t fm_val = sine_table[fm_index]; /* -128..127 */

      /* 调制相位偏移（小） */
      int32_t mod = (fm_val * (FMa0[ch] + ((int64_t)FMda[ch] * (int32_t)FMexp[ch] >> 16))) >> 8;
      /* 计算载波的最终相位索引 */
      uint8_t idx = (uint8_t)((phase_acc[ch] + (mod << 8)) >> 24); /* map to 0..255 */
      int32_t carrier = sine_table[idx]; /* -128..127 */

      /* 来自环境（envADSR 为 0..0xFFFF）和amp_base（体积）的幅度 */
      int32_t amp = ((int32_t)amp_base[ch] * (envADSR[ch] >> 8)) >> 8; /* 0..255 */
      int32_t voice = (carrier * amp) >> 8; /* approx -128..127 scaled */
      mix += voice;
    }

    /* 混合大约是 -512..512 也许;缩放到 DAC 范围 0..4095（12 位）*/
    int32_t out = mix; /* centered near 0 */
    /* --- 软件增益调整（尝试不同增益值） --- */
    const int GAIN = 4; /* 试 4, 6, 8 直到合适 */
    out = out * GAIN; /* 放大mix */
    /* 简单的夹紧和缩放 */
    if (out > 2047) out = 2047;
    if (out < -2048) out = -2048;
    uint16_t dacval = (uint16_t)((out + 2048) * 4095 / 4095); /* center to 0..4095 */
    /* 写入 DAC */
    HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dacval);
  }
}

/* --- 简单的助手：在下一个可用频道---开始笔记 */
static void handle_key_pressed(uint8_t k) {
  if (k >= 19) return;
  /* 仪器选择（如果是特殊键） - 根据需要实施 */
  if (k == 18) { /* 前ample：键 18 作为仪表开关 */
    current_instr++;
    if (current_instr >= NINSTR) current_instr = 0;
    return;
  }

  /* 找到类似于 Arduino 逻辑的 nextch */
  int nextch = -1;
  for (int i=0;i<NCH;i++){
    if (iADSR[i]>0 && keych[i]==k) { nextch = i; break; }
  }
  if (nextch == -1) {
    for (int i=0;i<NCH;i++){
      if (iADSR[i]==0) { nextch = i; break; }
    }
  }
  if (nextch == -1) {
    /* 偷走最旧的 */
    nextch = 0;
    for (int i=1;i<NCH;i++) if (tch[i] > tch[nextch]) nextch = i;
  }

  /* 初始化语音 */
  phase_acc[nextch] = 0;
  amp_base[nextch] = ldness[current_instr];
  /* inc_base: use pitch0 + key mapping (assume key index maps directly to semitone offset) */
  /* in original code, tone_inc[pitch0[instr] + keypressed] */
  int semitoneIndex = (int)pitch0[current_instr] + (int)k; /* ensure in range */
  if (semitoneIndex < 0) semitoneIndex = 0;
  if (semitoneIndex >= TONE_COUNT) semitoneIndex = TONE_COUNT-1;
  inc_base[nextch] = tone_inc[semitoneIndex];
  envADSR[nextch]=0;
  iADSR[nextch] = 1; /* attack */
  FMphase[nextch] = 0;
  FMinc_base[nextch] = ((int64_t)inc_base[nextch] * FM_inc_tab[current_instr]) / 256;
  FMa0[nextch] = FM_a2_tab[current_instr];
  FMda[nextch] = (int32_t)FM_a1_tab[current_instr] - (int32_t)FM_a2_tab[current_instr];
  FMexp[nextch] = 0xFFFFFFFF;
  FMdec[nextch] = FM_dec_tab[current_instr];
  keych[nextch] = k;
  tch[nextch] = 0;
}

static void handle_key_released(uint8_t k) {
  for (int i=0;i<NCH;i++){
    if (keych[i] == k) {
      iADSR[i] = 4; /* release */
    }
  }
}

/* ---初始化助手 --- */
static void init_sine_table(void) {
  for (int i=0;i<SINE_LEN;i++){
    double v = sin(2.0*M_PI*(i+0.5)/ (double)SINE_LEN);
    sine_table[i] = (int16_t)(v * 127.0); /* -127..127 */
  }
}

/* 计算从 MIDI 48 （C3） 开始的 48 个半音的tone_inc表 */
static void init_tone_inc(void) {
  for (int i=0;i<TONE_COUNT;i++){
    int midi = 48 + i; /* C3 = MIDI 48 */
    double f = midi_freq(midi);
    /* 32 位相位累加器在 SAMPLE_RATE 处的相位增量 */
    uint32_t inc = (uint32_t)((f * (double)( (uint64_t)1 << 32 )) / (double)SAMPLE_RATE + 0.5);
    tone_inc[i] = inc;
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DAC1_Init();
  MX_TIM6_Init();
  /* USER CODE BEGIN 2 */

  //合成器初始化 
  init_sine_table();
  init_tone_inc();

   //初始化声音 
  for (int i=0;i<NCH;i++) {
    phase_acc[i]=0;
    inc_base[i]=0;
    amp_base[i]=0;
    envADSR[i]=0;
    iADSR[i]=0;
    FMphase[i]=0;
    FMexp[i]=0xFFFFFFFF;
    keych[i]=KEY_NOKEY;
    tch[i]=0;
  }

  /* 启动 DAC 输出 - 无触发器，我们通过 ISR 中的 HAL 写入 */
  HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
  HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
  HAL_Delay(20);
  /* 启动 TIM6 中断（采样定时器） */
  HAL_TIM_Base_Start_IT(&htim6);


  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

    //每 ~5 毫秒进行一次简单的去抖动轮询
    HAL_Delay(5);
    for (int k=0;k<19;k++) {
      GPIO_PinState st = HAL_GPIO_ReadPin(keymap[k].port, keymap[k].pin);
      uint8_t pressed = (st == GPIO_PIN_RESET) ? 1 : 0; // assume pull-up
      if (pressed && !lastKeyStates[k]) {
        lastKeyStates[k]=1;
        handle_key_pressed((uint8_t)k);
      } else if (!pressed && lastKeyStates[k]) {
        lastKeyStates[k]=0;
        handle_key_released((uint8_t)k);
      }
    }
    /* 其他 UI：乐器更换可以是专用键或长按 */



    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1_BOOST);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = RCC_PLLM_DIV4;
  RCC_OscInitStruct.PLL.PLLN = 85;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
