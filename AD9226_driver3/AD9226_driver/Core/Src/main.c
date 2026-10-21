/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
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

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "ad9226.h"
#include "equivalent_sampling.h"
#include "spi.h"
#include "spicmd.h"
#include "screen_app.h"
#include "measurement_lock_policy.h"
#include "screen_protocol.h"
#include "screen_signal_model.h"
#include <stdio.h>
#include <math.h>
#include <string.h>  /* dump_frame_binary 用 memcpy */
#include <limits.h>  /* INT16_MIN / INT16_MAX */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
  MEASUREMENT_ACQUIRING = 0,
  MEASUREMENT_FROZEN,
  MEASUREMENT_ERROR
} MeasurementState;
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;

TIM_HandleTypeDef htim1;
DMA_HandleTypeDef hdma_tim1_up;
DMA_HandleTypeDef hdma_usart2_tx; /* USART2 TX:DMA1 Stream6 */

UART_HandleTypeDef huart1;
UART_HandleTypeDef huart2;   /* F4_acq -> F4_display 波形发送口 (PA2 TX / PA3 RX) */
extern UART_HandleTypeDef huart1;  /* __io_putchar 用 */
static volatile bool g_calibration_dump_req = false;
static uint32_t g_calibration_seq = 0U;
volatile bool g_dump_req = false;  /* 串口发 'd' 后触发下一帧波形打印(128 点,兼容 viewer) */
volatile bool g_raw_dump_req = false;  /* 'r' 后触发下一帧全 4096 点 raw dump,跳过 summary */
volatile uint32_t g_raw_dump_seq = 0;  /* raw dump 序列号,viewer 用来配对 */
volatile bool g_continuous_dump = false; /* 默认关闭, 'q' 开启 */
volatile bool g_print_summary_req = false;  /* 'p' 命令:打印下一帧 summary */
volatile uint32_t g_continuous_seq = 0;  /* continuous 镜像的序列号 */
volatile uint32_t g_uart_rx_flag = false; /* UART RX 中断收到字节 */
volatile uint8_t g_uart_rx_byte = 0;  /* 收到的字节 */
volatile uint16_t g_eq_dump_cycles = 0;  /* 'e' 命令:1 或 3,0 表示不 dump */
volatile uint32_t g_eq_seq = 0;          /* 等效采样波形批次号,每帧重建后 +1 */
volatile uint32_t g_screen_seq = 0;      /* 串口屏帧序列号,每 ADC 帧 +1 */
volatile uint8_t  g_hmi_rx_byte = 0;     /* HMI 串口屏 USART2 接收字节 */
static MeasurementState g_measurement_state = MEASUREMENT_ACQUIRING;
static uint32_t g_measurement_started_ms = 0u;
static uint32_t g_last_model_sequence = 0u;
volatile uint32_t g_measurement_elapsed_ms = 0u;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM1_Init(void);
static void MX_SPI1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_USART2_UART_Init(void);
/* USER CODE BEGIN PFP */
static void frame_stats(const uint16_t *buf, uint32_t n,
                        uint16_t *out_min, uint16_t *out_max,
                        uint32_t *out_mean, float *out_std);
static void dump_frame_csv(const uint16_t *buf, uint32_t dump_n);
static void dump_calibration_csv(const uint16_t *buf);
static void dump_frame_binary(const uint16_t *buf, uint32_t n,
                              uint32_t seq, uint32_t fs_hz);
static void dump_eq_waveform(uint16_t cycles);   /* 'e' 命令:dump 重建后 1/3 周期 */
static bool screen_fill_from_measurement_model(
                               ScreenDataFrame *frame,
                               const H743MeasurementModel *model,
                               uint8_t wave_periods)
{
    ScreenSignalSpectrum spectrum;
    const uint16_t wave_count = (wave_periods == (uint8_t)SCREEN_WAVE_PERIOD_1)
        ? SCREEN_SIGNAL_POINTS_PER_PERIOD
        : SCREEN_WAVE_MAX_POINTS;
    uint16_t index;

    if (!ScreenSignalModel_GenerateWaveCodes(
            model,
            wave_periods,
            frame->wave,
            SCREEN_WAVE_MAX_POINTS)
        || !ScreenSignalModel_FillSpectrum(model, &spectrum))
    {
        return false;
    }

    frame->frequency_mhz = model->fundamental_Hz * 1000U;
    frame->vpp_uv = model->upp_uV;
    frame->vrms_uv = model->urms_uV;
    frame->wave_count = wave_count;
    frame->fft_count = SCREEN_FFT_MAX_POINTS;
    frame->measurement_status = model->status;
    frame->measurement_quality_flags = model->quality_flags;

    for (index = 0U; index < (uint16_t)spectrum.count; ++index)
    {
        const uint32_t frequency_mHz = spectrum.line[index].frequency_mHz;
        const uint32_t amplitude_uV = spectrum.line[index].amplitude_peak_uV;

        frame->harmonic_freq_mhz[index] = frequency_mHz;
        frame->harmonic_peak_uv[index] = amplitude_uV;
    }
    if (!ScreenSignalModel_RasterizeSpectrum(
            &spectrum,
            500000000U,
            frame->fft,
            SCREEN_FFT_MAX_POINTS))
    {
        return false;
    }
    return true;
}

static void screen_build_frame(ScreenDataFrame *frame,
                               const uint16_t *adc_buf,
                               uint32_t adc_count,
                               const int16_t *period,
                               uint32_t seq,
                               uint8_t wave_periods);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

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
  MX_DMA_Init();
  MX_TIM1_Init();
  MX_SPI1_Init();
  MX_USART1_UART_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  /*
   * 注意:STM32F4 没有 F1 那样的 AFIO SWJ remap 寄存器。
   * PA15/JTDI 是 JTAG 输入引脚(不驱动),直接配置为 GPIO 输入即可用作按键,
   * 同时保留 SWD(PA13/PA14)调试能力。无需调用 __HAL_AFIO_REMAP_SWJ_NOJTAG()。
   */

  /* PA4 = H7_READY 输入:等 H7 拉高才开始发 SPI1 数据
   * PA4 之前是 SPI NSS 候选(暂未用),改成 Input + 内部上拉
   * 不选 PA10:USART1_RX 在板上被 USB-TTL (CH340) 接到,
   *       CH340 TX 输出低,PA10 实际被外部拉低,内部上拉不够强
   * 内部上拉: H7 没接时 PA4=1,兼容"无脑发"行为(L5 自环/L4 冒烟不破) */
  {
      GPIO_InitTypeDef gpio = {0};
      gpio.Pin   = GPIO_PIN_4;
      gpio.Mode  = GPIO_MODE_INPUT;
      gpio.Pull  = GPIO_PULLUP;          /* 内部上拉,~40kΩ */
      gpio.Speed = GPIO_SPEED_FREQ_LOW;
      HAL_GPIO_Init(GPIOA, &gpio);

      /* PA15 = 按键输入，控制屏幕 1/3 周期显示 */
      gpio.Pin   = GPIO_PIN_15;
      gpio.Mode  = GPIO_MODE_INPUT;
      gpio.Pull  = GPIO_PULLUP;
      gpio.Speed = GPIO_SPEED_FREQ_LOW;
      HAL_GPIO_Init(GPIOA, &gpio);
  }
  printf("[1] UART OK (H7_READY=PA4, KEY=PA15, pullup)\r\n");

  /* 启动 AD9226: PE9 出 2.048 MHz, 配好 DMA 通道
   * (CubeMX 把 TIM1_CH1 配在 PE9,不是默认 PA8) */
  AD9226_Init();
  printf("[2] AD9226_Init OK, Fs=%lu Hz\r\n",
         (unsigned long)AD9226_GetSampleRate());
  printf("[ADC12] raw capture enabled; send c for 4096 raw codes\r\n");
  /* 初始化命令解析模块:H7 的命令通过 SPI1 全双工命令槽回传 */
  SpiCmd_Init();
  printf("[2b] SpiCmd_Init OK\r\n");
  /* 初始化串口屏应用层 (USART2 PA2/PA3, 按键 PA15) */
  ScreenApp_Init();
  ScreenApp_SetWavePeriods(SCREEN_WAVE_PERIOD_3);
  printf("[2c] ScreenApp_Init OK (USART2 PA2/PA3 -> TJC HMI)\r\n");
  /* 启动第一次 4096 点采集 */
  g_measurement_state = MEASUREMENT_ACQUIRING;
  g_measurement_started_ms = HAL_GetTick();
  g_last_model_sequence = 0u;
  g_measurement_elapsed_ms = 0u;
  AD9226_Start();
  /* 启动 USART2 接收中断 (HMI 串口屏触摸/状态回传) */
  HAL_UART_Receive_IT(&huart2, (uint8_t *)&g_hmi_rx_byte, 1);
  /* 启动 USART1 接收中断(用于接收 PC 端 'd' 命令) */
  HAL_UART_Receive_IT(&huart1, (uint8_t *)&g_uart_rx_byte, 1);
  printf("[3] AD9226_Start OK, entering loop\r\n");
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* 砍掉 [loop] alive: 之前 50000 iter 喷一次,也是占喷速。F407 活着靠 'p' 命令主动查询 */
    /* 串口命令:
     *   'd'/'D' → 下次 128 点 CSV dump (兼容旧 viewer)
     *   'r'/'R' → 下次全 4096 点 binary dump (一次性)
     *   'q'/'Q' → 切换 continuous binary 镜像 (默认 ON,关闭后 F407→H7 恢复 500fps)
     *   'p'/'P' → 打印当前一帧 summary(不影响 binary 镜像)
     *   'e'/'E' → 等效采样:输出 1 周期(256 点)重建波形 CSV
     *   'f'/'F' → 等效采样:输出 3 周期(768 点)重建波形 CSV */
    if (g_uart_rx_flag) {
      g_uart_rx_flag = false;
      uint8_t rxch = g_uart_rx_byte;
      if (rxch == 'd' || rxch == 'D') {
        g_dump_req = true;
        /* printf 拿掉: 镜像模式喷 binary 期间 printf 会乱字节流,viewer 会错位 */
      } else if (rxch == 'r' || rxch == 'R') {
        g_raw_dump_req = true;
        g_raw_dump_seq++;
        /* printf 拿掉,同上 */
      } else if (rxch == 'q' || rxch == 'Q') {
        g_continuous_dump = !g_continuous_dump;
        printf("[DUMP] continuous = %d (q to toggle)\r\n", (int)g_continuous_dump);
      } else if (rxch == 'p' || rxch == 'P') {
        /* 'p' = 打印一帧 summary 排队等待,如果在 binary 镜像期间会被切碎,
         * 但用户主动发 'p' 表示要看状态,这是他期望的 */
        g_print_summary_req = true;
      } else if (rxch == 'e' || rxch == 'E') {
        /* 'e' = dump 等效采样重建后的 1 周期波形 */
        g_eq_dump_cycles = 1U;
      } else if (rxch == 'f' || rxch == 'F') {
        /* 'f' = dump 等效采样重建后的 3 周期波形 */
        g_eq_dump_cycles = 3U;
      }
      /* 接收已在 HAL_UART_RxCpltCallback 中重新启动,这里不需要再开 */
    }
    if (g_calibration_dump_req) {
      g_continuous_dump = false;
      g_raw_dump_req = false;
      g_dump_req = false;
      g_eq_dump_cycles = 0U;
      if (g_measurement_state != MEASUREMENT_ACQUIRING) {
        if (HAL_DMA_GetState(&hdma_tim1_up) == HAL_DMA_STATE_BUSY) {
          g_calibration_dump_req = false;
          printf("[CAL12 ERROR] DMA busy; retry c after capture completes\r\n");
        } else {
          g_measurement_state = MEASUREMENT_ACQUIRING;
          g_measurement_started_ms = HAL_GetTick();
          AD9226_Start();
        }
      }
    }
    if ((g_measurement_state == MEASUREMENT_ACQUIRING) && AD9226_IsDone()) {
      /* GPIO bit12~bit15 are unrelated pins; ADC bit0~bit11 stay unchanged. */
      if (!AD9226_FinalizeFrame()) {
        printf("[ADC] frame not complete\r\n");
        if ((HAL_GetTick() - g_measurement_started_ms) >= 2000u) {
          g_measurement_state = MEASUREMENT_ERROR;
          g_measurement_elapsed_ms = HAL_GetTick() - g_measurement_started_ms;
          ScreenApp_ShowMeasurementError();
          printf("[MEAS] ERROR capture timeout\r\n");
        } else {
          AD9226_Start();
        }
        continue;
      }
      const uint16_t *buf = AD9226_GetBuf();
      uint32_t fs_hz = AD9226_GetSampleRate();
      if (g_calibration_dump_req) {
        g_calibration_dump_req = false;
        g_measurement_state = MEASUREMENT_FROZEN;
        dump_calibration_csv(buf);
        continue;
      }

      /* === 主流程: SPI1 全双工一帧 8208 B, 同时发样本到 H7 并收 H7 命令 ===
       * 帧格式: [16 B 命令槽][8192 B 样本], 命令槽由 H7 通过 MISO 发回。
       * 不再喷每帧 summary(用户主动 'p' 才看一帧) */

      uint8_t cmd_slot[ACQ_SPI_CMD_SLOT_BYTES];
      HAL_StatusTypeDef spi_st = AcqSpi_SendSamples(
          buf, AD9226_SAMPLE_COUNT, fs_hz);
      if (spi_st != HAL_OK) {
        printf("[SPI1] frame exchange FAILED st=%d\r\n", (int)spi_st);
      } else {
        HAL_StatusTypeDef cmd_st = AcqSpi_RecvCmd(
            cmd_slot, sizeof(cmd_slot));
        if (cmd_st != HAL_OK) {
          printf("[SPI1] recv cmd slot FAILED st=%d\r\n", (int)cmd_st);
        } else {
          SpiCmd_ProcessRx(cmd_slot, sizeof(cmd_slot));
        }
      }

      /* 等效采样模式:重建 1 个基频周期,供显示 F4 使用(同时保留 raw 给 H7 计算) */
      if (AD9226_IsEqMode()) {
        static int16_t s_signed_buf[AD9226_SAMPLE_COUNT];
        for (uint32_t i = 0; i < AD9226_SAMPLE_COUNT; i++) {
          /* AD9226 单端输入,中点零码约 2048;转成 signed 方便重建 */
          int32_t v = (int32_t)(buf[i] & 0x0FFFU) - 2048L;
          if (v < INT16_MIN) v = INT16_MIN;
          else if (v > INT16_MAX) v = INT16_MAX;
          s_signed_buf[i] = (int16_t)v;
        }
        static int16_t s_period[ES_PERIOD_POINTS];
        g_eq_seq++;
        if (EsReconstructPeriod(s_signed_buf,
                                AD9226_GetEqFundamental(),
                                AD9226_GetEqDivider(),
                                s_period)) {
          EsWaveform_Publish(s_period,
                             g_eq_seq,
                             AD9226_GetEqFundamental(),
                             AD9226_GetEqDivider());

          /* 构造串口屏数据帧，交给 screen_app 在下一周期渲染。
           * 若实际采样率已足够高（每周期 >=10 点），优先用普通重建，
           * 避免等效采样的暂态/谐波混叠让波形看起来是三角波。 */
          if (g_eq_dump_cycles != 0) {
            dump_eq_waveform(g_eq_dump_cycles);
            g_eq_dump_cycles = 0;
          }
        } else {
          printf("[EQ] reconstruct failed\r\n");
        }
      }

      /* 非等效采样模式时,也用 raw ADC 构造一帧给串口屏(优先用 H7 测量基频重建,否则直接抽取) */
      /* 正式显示只接受 H743 拼齐后的模型，不再回退到 raw ADC 波形。 */
      {
        H743MeasurementModel measurement_model;
        if (SpiCmd_GetMeasurementModel(&measurement_model) &&
            (measurement_model.published_sequence != g_last_model_sequence))
        {
          const uint32_t elapsed_ms = HAL_GetTick() - g_measurement_started_ms;
          const bool displayable = H743MeasurementModel_IsDisplayable(&measurement_model);
          const MeasurementLockDecision lock_decision = MeasurementLockPolicy_Decide(
              true, displayable, elapsed_ms, 2000u);

          g_last_model_sequence = measurement_model.published_sequence;
          if (lock_decision == MEASUREMENT_LOCK_FREEZE)
          {
            static ScreenDataFrame s_screen_frame;
            g_screen_seq++;
            screen_build_frame(
                &s_screen_frame,
                buf, AD9226_SAMPLE_COUNT,
                NULL,
                g_screen_seq,
                (uint8_t)SCREEN_WAVE_PERIOD_3);
            ScreenApp_SetFrame(&s_screen_frame);
            g_measurement_state = MEASUREMENT_FROZEN;
            g_measurement_elapsed_ms = HAL_GetTick() - g_measurement_started_ms;
            printf("[MEAS] LOCK seq=%lu elapsed=%lums f0=%luHz components=%u\r\n",
                   (unsigned long)measurement_model.published_sequence,
                   (unsigned long)g_measurement_elapsed_ms,
                   (unsigned long)measurement_model.fundamental_Hz,
                   (unsigned)measurement_model.component_count);
          }
          else if (lock_decision == MEASUREMENT_LOCK_TIMEOUT)
          {
            g_measurement_state = MEASUREMENT_ERROR;
            g_measurement_elapsed_ms = HAL_GetTick() - g_measurement_started_ms;
            ScreenApp_ShowMeasurementError();
            printf("[MEAS] ERROR status=%u f0=%lu upp=%lu components=%u\r\n",
                   (unsigned)measurement_model.status,
                   (unsigned long)measurement_model.fundamental_Hz,
                   (unsigned long)measurement_model.upp_uV,
                   (unsigned)measurement_model.component_count);
          }
          else
          {
            /* H743 上电首轮可能先返回空模型；继续送样，等待下一组有效五页。 */
            printf("[MEAS] WAIT invalid startup model seq=%lu status=%u\r\n",
                   (unsigned long)measurement_model.published_sequence,
                   (unsigned)measurement_model.status);
          }
        }
      }

      /* binary 镜像 (默认开, 'q' 关):和 H7 收到同一份数据,viewer 字节级对照 */
      if (g_continuous_dump) {
        g_continuous_seq++;
        dump_frame_binary(buf, AD9226_SAMPLE_COUNT, g_continuous_seq, fs_hz);
      }

      /* 'r' 命令:本帧额外补一次 one-shot binary dump (此时 continuous 已发过一次,
       * viewer 收到的就是同一帧的 2 份 binary, 用 seq 区分) */
      if (g_raw_dump_req) {
        g_raw_dump_req = false;
        dump_frame_binary(buf, AD9226_SAMPLE_COUNT, g_raw_dump_seq, fs_hz);
      }

      /* 'd' 命令:老 viewer 兼容,128 点 CSV (binary 模式下 printf 切字节流, 慎用) */
      if (g_dump_req) {
        g_dump_req = false;
        dump_frame_csv(buf, 128);
      }

      /* 'p' 命令: 用户主动要一帧 summary,这里 printf 出去 */
      if (g_print_summary_req) {
        g_print_summary_req = false;
        uint16_t mn, mx; uint32_t mean; float std;
        frame_stats(buf, AD9226_SAMPLE_COUNT, &mn, &mx, &mean, &std);
        int32_t std_x100 = (int32_t)(std * 100.0f + 0.5f);
        if (std_x100 < 0) std_x100 = 0;
        printf("seq=%lu mean=%lu min=%u max=%u pp=%u std=%ld.%02ld Fs=%lu cont=%d bits=12 raw=1\r\n",
               (unsigned long)g_continuous_seq, (unsigned long)mean,
               mn, mx, (unsigned)(mx - mn),
               (long)(std_x100 / 100), (long)(std_x100 % 100),
               (unsigned long)fs_hz, (int)g_continuous_dump);
      }

      /* 启下一轮 */
      if (g_measurement_state == MEASUREMENT_ACQUIRING) {
        AD9226_Start();
      }
    }

    if ((g_measurement_state == MEASUREMENT_ACQUIRING) &&
        ((HAL_GetTick() - g_measurement_started_ms) >= 2000u)) {
      g_measurement_state = MEASUREMENT_ERROR;
      g_measurement_elapsed_ms = HAL_GetTick() - g_measurement_started_ms;
      ScreenApp_ShowMeasurementError();
      printf("[MEAS] ERROR response timeout\r\n");
    }

    /* 冻结后p查看当前原始码摘要，c请求新一帧原始码。 */
    if ((g_measurement_state == MEASUREMENT_FROZEN) && g_print_summary_req) {
      const uint16_t *buf = AD9226_GetBuf();
      uint16_t mn, mx;
      uint32_t mean;
      float std;
      int32_t std_x100;

      g_print_summary_req = false;
      frame_stats(buf, AD9226_SAMPLE_COUNT, &mn, &mx, &mean, &std);
      std_x100 = (int32_t)(std * 100.0f + 0.5f);
      if (std_x100 < 0) std_x100 = 0;
      printf("frozen mean=%lu min=%u max=%u pp=%u std=%ld.%02ld Fs=%lu bits=12 raw=1\r\n",
             (unsigned long)mean, mn, mx, (unsigned)(mx - mn),
             (long)(std_x100 / 100), (long)(std_x100 % 100),
             (unsigned long)AD9226_GetSampleRate());
    }

    /* 串口屏任务：按键去抖、HMI 通信、按需渲染波形/测量值 */
    ScreenApp_Task();

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
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;  /*!< HSE=8 MHz real on this board, 8/8*336/2=168 MHz SYSCLK */
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 4;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_16;  /* 84 MHz / 16 = 5.25 MHz */
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM1_Init(void)
{

  /* USER CODE BEGIN TIM1_Init 0 */

  /* USER CODE END TIM1_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM1_Init 1 */

  /* USER CODE END TIM1_Init 1 */
  htim1.Instance = TIM1;
  htim1.Init.Prescaler = 0;
  htim1.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim1.Init.Period = 81;
  htim1.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim1.Init.RepetitionCounter = 0;
  htim1.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;  /* 改 ARR 在下个 update event 生效,避免短脉冲 */
  if (HAL_TIM_Base_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim1, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim1) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_ENABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim1, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 41;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim1, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim1, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM1_Init 2 */

  /* USER CODE END TIM1_Init 2 */
  HAL_TIM_MspPostInit(&htim1);

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  /* 升 921600 (8x 115200),原因:镜像 4096 点 raw 到 UART 8KB/帧,
   * 115200 baud 一帧要 712ms → F407→H7 链路被拖累到 1.4 fps;
   * 921600 → 71ms/帧 → 14 fps,够调试用。 */
  huart1.Init.BaudRate = 921600;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * @brief USART2 Initialization Function
  * @param None
  * @retval None
  * @note PA2=USART2_TX, PA3=USART2_RX, 直连屏幕 F4 的波形输入口
  */
static void MX_USART2_UART_Init(void)
{
  /* 使能 GPIOA / USART2 时钟 */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_USART2_CLK_ENABLE();

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  GPIO_InitStruct.Pin       = GPIO_PIN_2;                 /* PA2 TX */
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  GPIO_InitStruct.Pull      = GPIO_NOPULL;
  GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
  GPIO_InitStruct.Alternate = GPIO_AF7_USART2;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  GPIO_InitStruct.Pin       = GPIO_PIN_3;                 /* PA3 RX */
  GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  huart2.Instance        = USART2;
  huart2.Init.BaudRate   = 921600;                     /* TJC 串口屏已配置为 921600 */
  huart2.Init.WordLength = UART_WORDLENGTH_8B;
  huart2.Init.StopBits   = UART_STOPBITS_1;
  huart2.Init.Parity     = UART_PARITY_NONE;
  huart2.Init.Mode       = UART_MODE_TX_RX;              /* TX 发命令/波形,RX 收触摸/状态 */
  huart2.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
  huart2.Init.OverSampling = UART_OVERSAMPLING_16;
  if (HAL_UART_Init(&huart2) != HAL_OK)
  {
    Error_Handler();
  }

  /* 使能 USART2 中断 (HMI 触摸回传) */
  HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();   /* USART2_TX 用 DMA1 Stream6 */
  __HAL_RCC_DMA2_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA2_Stream5_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA2_Stream5_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA2_Stream5_IRQn);

  /* ★ .ioc 漏了 Request 字段导致 MX_DMA_Init 缺 HAL_DMA_Init,手动补(本 HAL 版本无 Request,用 Channel 字段) */
  hdma_tim1_up.Instance                      = DMA2_Stream5;
  hdma_tim1_up.Init.Channel                  = DMA_CHANNEL_6;          /* Stream5 ch6 = TIM1_UP */
  hdma_tim1_up.Init.Direction                = DMA_PERIPH_TO_MEMORY;
  hdma_tim1_up.Init.PeriphInc                = DMA_PINC_DISABLE;
  hdma_tim1_up.Init.MemInc                   = DMA_MINC_ENABLE;
  hdma_tim1_up.Init.PeriphDataAlignment      = DMA_PDATAALIGN_HALFWORD;
  hdma_tim1_up.Init.MemDataAlignment         = DMA_MDATAALIGN_HALFWORD;
  hdma_tim1_up.Init.Mode                     = DMA_NORMAL;
  hdma_tim1_up.Init.Priority                 = DMA_PRIORITY_HIGH;
  hdma_tim1_up.Init.FIFOMode                 = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_tim1_up) != HAL_OK)
  {
    Error_Handler();
  }
  /* 把 hdma_tim1_up 关联到 TIM1(MSP 缺失部分) */
  __HAL_LINKDMA(&htim1, hdma[TIM_DMA_ID_UPDATE], hdma_tim1_up);
  /* 用 HAL 库新版 API 注册 DMA 完成回调(引用 ad9226.c 里的 HAL_DMA_XferCpltCallback,防止死链) */
  extern void HAL_DMA_XferCpltCallback(DMA_HandleTypeDef *hdma);
  HAL_DMA_RegisterCallback(&hdma_tim1_up, HAL_DMA_XFER_CPLT_CB_ID, HAL_DMA_XferCpltCallback);

  /* USART2 TX DMA:DMA1 Stream6 Channel 4 = USART2_TX */
  hdma_usart2_tx.Instance                      = DMA1_Stream6;
  hdma_usart2_tx.Init.Channel                  = DMA_CHANNEL_4;
  hdma_usart2_tx.Init.Direction                = DMA_MEMORY_TO_PERIPH;
  hdma_usart2_tx.Init.PeriphInc                = DMA_PINC_DISABLE;
  hdma_usart2_tx.Init.MemInc                   = DMA_MINC_ENABLE;
  hdma_usart2_tx.Init.PeriphDataAlignment      = DMA_PDATAALIGN_BYTE;
  hdma_usart2_tx.Init.MemDataAlignment         = DMA_MDATAALIGN_BYTE;
  hdma_usart2_tx.Init.Mode                     = DMA_NORMAL;
  hdma_usart2_tx.Init.Priority                 = DMA_PRIORITY_LOW;
  hdma_usart2_tx.Init.FIFOMode                 = DMA_FIFOMODE_DISABLE;
  if (HAL_DMA_Init(&hdma_usart2_tx) != HAL_OK)
  {
    Error_Handler();
  }
  __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);

  /* DMA1 Stream6 NVIC */
  HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 6, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOF_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOE_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET);

  /*Configure GPIO pins : PF0 PF1 PF2 PF3
                           PF4 PF5 PF6 PF7
                           PF8 PF9 PF10 PF11 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5|GPIO_PIN_6|GPIO_PIN_7
                          |GPIO_PIN_8|GPIO_PIN_9|GPIO_PIN_10|GPIO_PIN_11;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOF, &GPIO_InitStruct);

  /*Configure GPIO pin : PC12 (AD9226 OTR) */
  GPIO_InitStruct.Pin = GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA4 */
  GPIO_InitStruct.Pin = GPIO_PIN_4;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* printf → USART1 重定向 (CubeMX 生成的 syscalls.c 里的 _write 走 __io_putchar,不走 fputc) */
int __io_putchar(int ch)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)&ch, 1, HAL_MAX_DELAY);
    return ch;
}

/* USART1/2 接收完成中断回调:USART1 给 PC 调试命令,USART2 给 HMI 串口屏 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart1) {
        if ((g_uart_rx_byte == 'c') || (g_uart_rx_byte == 'C')) {
            g_calibration_dump_req = true;
        }
        g_uart_rx_flag = true;
        HAL_UART_Receive_IT(&huart1, (uint8_t *)&g_uart_rx_byte, 1);
    } else if (huart == &huart2) {
        HMI_RxByte(g_hmi_rx_byte);
        HAL_UART_Receive_IT(&huart2, (uint8_t *)&g_hmi_rx_byte, 1);
    }
}

/* 构造串口屏波形数据帧:把 256 点单周期重复成 768 点(3 周期) */
static void screen_extract_harmonics(ScreenDataFrame *frame,
                                     const uint32_t *spectrum,
                                     uint32_t fs_hz,
                                     uint32_t f0_hz,
                                     uint16_t vpp_mv)
{
    const uint16_t max_bin = ES_PERIOD_POINTS / 2u;
    const uint16_t low_bin = 2u;            /* 避开 DC 泄漏到 bin1 */
    const uint16_t search_win = 2u;
    uint32_t fundamental_mag = 0u;
    uint16_t i;

    if ((frame == NULL) || (spectrum == NULL) ||
        (fs_hz == 0u) || (f0_hz == 0u) || (vpp_mv == 0u))
    {
        return;
    }

    /* 用 H7 测得的基频定位 bin，只在 ±2 个 bin 内找真实峰值 */
    {
        uint16_t expected_bin = (uint16_t)(((uint64_t)f0_hz * ES_PERIOD_POINTS
                                            + (fs_hz >> 1u)) / fs_hz);
        uint16_t win_start;
        uint16_t win_end;

        if ((expected_bin < low_bin) || (expected_bin >= max_bin))
        {
            return;
        }

        win_start = (expected_bin > search_win) ? (expected_bin - search_win) : low_bin;
        win_end = expected_bin + search_win;
        if (win_end >= max_bin)
        {
            win_end = max_bin - 1u;
        }

        for (i = win_start; i <= win_end; i++)
        {
            if (spectrum[i] > fundamental_mag)
            {
                fundamental_mag = spectrum[i];
            }
        }
    }

    if (fundamental_mag == 0u)
    {
        return;
    }

    /* 基频：频率直接使用 H7 测得的 f0，幅度用 H7 Vpp */
    frame->harmonic_freq_mhz[0] = f0_hz * 1000u;
    frame->harmonic_peak_uv[0]   =
        (uint32_t)(((uint64_t)vpp_mv * 1000ULL) / 2ULL);

    /* 2、3 次谐波：按基频整数倍定位 bin，频率直接写 f0 的整数倍 */
    for (uint8_t h = 2u; h <= 3u; h++)
    {
        uint32_t fh = f0_hz * (uint32_t)h;
        uint16_t expected_bin = (uint16_t)(((uint64_t)fh * ES_PERIOD_POINTS
                                            + (fs_hz >> 1u)) / fs_hz);
        uint16_t win_start;
        uint16_t win_end;
        uint32_t peak_mag = 0u;

        if ((expected_bin < low_bin) || (expected_bin >= max_bin))
        {
            continue;
        }

        win_start = (expected_bin > search_win) ? (expected_bin - search_win) : low_bin;
        win_end = expected_bin + search_win;
        if (win_end >= max_bin)
        {
            win_end = max_bin - 1u;
        }

        for (i = win_start; i <= win_end; i++)
        {
            if (spectrum[i] > peak_mag)
            {
                peak_mag = spectrum[i];
            }
        }

        /* 谐波幅度至少为基频的 5% 才显示 */
        if (peak_mag >= (fundamental_mag / 20u))
        {
            frame->harmonic_freq_mhz[h - 1u] = fh * 1000u;
            {
                uint64_t harmonic_vpp_uv = ((uint64_t)vpp_mv * 1000ULL * (uint64_t)peak_mag)
                                           / (uint64_t)fundamental_mag;
                frame->harmonic_peak_uv[h - 1u] =
                    (uint32_t)(harmonic_vpp_uv / 2ULL);
            }
        }
    }

    /* 调试用：每秒输出一次谐波提取状态，便于观察数值是否稳定 */
    {
        static uint32_t s_last_harmonic_dbg = 0u;
        uint32_t now = HAL_GetTick();
        if ((now - s_last_harmonic_dbg) >= 1000u)
        {
            s_last_harmonic_dbg = now;
            printf("[HARM] fs=%lu f0=%lu vpp=%u mV | "
                   "fund_mag=%lu | rms1=%lu uV rms2=%lu uV rms3=%lu uV\r\n",
                   (unsigned long)fs_hz, (unsigned long)f0_hz, (unsigned int)vpp_mv,
                   (unsigned long)fundamental_mag,
                   (unsigned long)frame->harmonic_peak_uv[0],
                   (unsigned long)frame->harmonic_peak_uv[1],
                   (unsigned long)frame->harmonic_peak_uv[2]);
        }
    }
}

/* 小信号阈值：Vpp < 150mV 时尝试正弦拟合显示 */
#define SCREEN_SMALL_SIGNAL_VPP_UV      150000u
/* 极小信号阈值：Vpp < 80mV 时强制拟合，避免量化台阶无法观看 */
#define SCREEN_TINY_SIGNAL_VPP_UV        80000u
/* 允许谐波总量不超过基波的 1/4 */
#define SCREEN_SINE_FIT_HARMONIC_RATIO  4u

/* 对 256 点单周期做最小二乘正弦拟合，输出平滑的理想正弦 */
static void screen_sine_fit_period(const int16_t *in, int16_t *out, uint16_t points)
{
    float sum = 0.0f;
    float sum_sin = 0.0f;
    float sum_cos = 0.0f;
    float offset, a, b;
    const float scale = 2.0f * (float)M_PI / (float)points;
    uint16_t i;

    for (i = 0u; i < points; i++)
    {
        float y = (float)in[i];
        float ang = scale * (float)i;
        sum += y;
        sum_sin += y * sinf(ang);
        sum_cos += y * cosf(ang);
    }

    offset = sum / (float)points;
    a = 2.0f * sum_sin / (float)points;
    b = 2.0f * sum_cos / (float)points;

    for (i = 0u; i < points; i++)
    {
        float ang = scale * (float)i;
        float y = offset + a * sinf(ang) + b * cosf(ang);
        if (y > 2047.0f)
        {
            y = 2047.0f;
        }
        else if (y < -2048.0f)
        {
            y = -2048.0f;
        }
        out[i] = (int16_t)lroundf(y);
    }
}

static bool screen_should_sine_fit(const ScreenDataFrame *frame)
{
    uint64_t harmonic_rms;

    if (frame == NULL)
    {
        return false;
    }
    /* 80mVpp 以下强制拟合，否则小信号只剩几个 ADC 码无法看 */
    if (frame->vpp_uv < SCREEN_TINY_SIGNAL_VPP_UV)
    {
        return true;
    }
    if (frame->vpp_uv >= SCREEN_SMALL_SIGNAL_VPP_UV)
    {
        return false;
    }
    if (frame->harmonic_peak_uv[0] == 0u)
    {
        /* 谐波/基波能量未算出（H7 数据弱或 FFT 失败），仍做拟合保证平滑 */
        return true;
    }

    harmonic_rms = (uint64_t)frame->harmonic_peak_uv[1]
                 + (uint64_t)frame->harmonic_peak_uv[2];

    /* 谐波总量 < 基波 / 4 时认为接近正弦，可以做拟合 */
    return ((harmonic_rms * SCREEN_SINE_FIT_HARMONIC_RATIO)
            <= (uint64_t)frame->harmonic_peak_uv[0]);
}

/* 重建失败时的最终回退：根据 H7 基频和 raw ADC 幅值合成一个理想正弦周期，
 * 保证极小信号或噪声导致零穿越检测失败时仍有平滑波形。
 * 注意：该回退会丢失谐波信息，仅用于保证时域波形可读。 */
static void screen_synth_sine_period(int16_t *period,
                                     uint32_t freq_hz,
                                     uint16_t adc_min,
                                     uint16_t adc_max)
{
    const uint16_t points = ES_PERIOD_POINTS;
    const float scale = 2.0f * (float)M_PI / (float)points;
    int32_t offset = ((int32_t)adc_min + (int32_t)adc_max) / 2 - 2048L;
    int32_t amp = ((int32_t)adc_max - (int32_t)adc_min) / 2;
    uint16_t i;

    (void)freq_hz;  /* 频率仅用于屏幕标签，合成相位从 0 开始 */

    if (amp < 1)
    {
        amp = 1;    /* 至少保留一条有起伏的曲线 */
    }

    for (i = 0u; i < points; i++)
    {
        float y = (float)offset + (float)amp * sinf(scale * (float)i);
        if (y > 2047.0f)
        {
            y = 2047.0f;
        }
        else if (y < -2048.0f)
        {
            y = -2048.0f;
        }
        period[i] = (int16_t)lroundf(y);
    }
}

static void screen_upsample_period_768(const uint16_t *src, uint16_t *dst)
{
    const uint16_t out_points = SCREEN_WAVE_MAX_POINTS;
    const uint16_t in_points  = ES_PERIOD_POINTS;
    uint16_t i;

    for (i = 0u; i < out_points; i++)
    {
        uint32_t pos = ((uint32_t)i * (in_points - 1u)) / (out_points - 1u);
        uint32_t rem = ((uint32_t)i * (in_points - 1u)) % (out_points - 1u);
        uint16_t idx = (uint16_t)pos;
        uint32_t w1  = (out_points - 1u) - rem;
        uint32_t w2  = rem;
        uint32_t v;

        if ((idx + 1u) < in_points)
        {
            v = ((uint32_t)src[idx] * w1 + (uint32_t)src[idx + 1u] * w2) / (out_points - 1u);
        }
        else
        {
            v = src[idx];
        }
        dst[i] = (uint16_t)v;
    }
}

/* 小信号显示增益：动态缩放本身会让小信号充满屏幕，但 ADC 码值太少时
 * 曲线会出现明显台阶。对振幅 <64 码的周期做显示域放大，只影响视觉平滑度，
 * 不影响 H7 测得的 Vpp / 频率标签。 */
#define SCREEN_DISPLAY_MIN_RANGE_CODES  64u

static void screen_normalize_small_period(const int16_t *in,
                                          int16_t *out,
                                          uint16_t points)
{
    int16_t min_v = in[0];
    int16_t max_v = in[0];
    int32_t range;
    uint16_t i;

    for (i = 1u; i < points; i++)
    {
        if (in[i] < min_v)
        {
            min_v = in[i];
        }
        if (in[i] > max_v)
        {
            max_v = in[i];
        }
    }

    range = (int32_t)max_v - (int32_t)min_v;
    if (range < 1)
    {
        range = 1;
    }

    if (range >= (int32_t)SCREEN_DISPLAY_MIN_RANGE_CODES)
    {
        (void)memcpy(out, in, (size_t)points * sizeof(int16_t));
        return;
    }

    {
        const int32_t target = (int32_t)SCREEN_DISPLAY_MIN_RANGE_CODES;
        const int32_t offset = ((int32_t)max_v + (int32_t)min_v) / 2;
        for (i = 0u; i < points; i++)
        {
            int32_t y = offset + (((int32_t)in[i] - offset) * target) / range;
            if (y > 2047)
            {
                y = 2047;
            }
            else if (y < -2048)
            {
                y = -2048;
            }
            out[i] = (int16_t)y;
        }
    }
}

static void screen_fill_wave_buffer(ScreenDataFrame *frame,
                                    const int16_t *period,
                                    uint8_t wave_periods)
{
    uint16_t i;
    int16_t norm_period[ES_PERIOD_POINTS];

    screen_normalize_small_period(period, norm_period, ES_PERIOD_POINTS);

    if (wave_periods == (uint8_t)SCREEN_WAVE_PERIOD_1)
    {
        uint16_t offset_period[ES_PERIOD_POINTS];
        for (i = 0u; i < ES_PERIOD_POINTS; i++)
        {
            offset_period[i] = (uint16_t)(norm_period[i] + 2048);
        }
        screen_upsample_period_768(offset_period, frame->wave);
    }
    else
    {
        for (i = 0u; i < SCREEN_WAVE_MAX_POINTS; i++)
        {
            frame->wave[i] = (uint16_t)(norm_period[i % ES_PERIOD_POINTS] + 2048);
        }
    }
}



static void screen_build_frame(ScreenDataFrame *frame,
                               const uint16_t *adc_buf,
                               uint32_t adc_count,
                               const int16_t *period,
                               uint32_t seq,
                               uint8_t wave_periods)
{
    H743MeasurementModel measurement_model;
    bool model_available = SpiCmd_GetMeasurementModel(&measurement_model)
        && H743MeasurementModel_IsDisplayable(&measurement_model);
    uint32_t freq_hz = 0u;
    uint16_t vpp_mv  = 0u;
    bool meas_valid = SpiCmd_GetMeasurement(&freq_hz, &vpp_mv);
    uint32_t fs_hz  = AD9226_GetSampleRate();
    uint16_t divider = (uint16_t)(ES_TIMER_CLOCK_HZ / fs_hz);
    uint16_t i;
    bool reconstructed = false;
    static int16_t s_period[ES_PERIOD_POINTS];
    static uint32_t raw_spectrum[ES_PERIOD_POINTS];
    static uint16_t display_spectrum[ES_PERIOD_POINTS];
    uint16_t adc_min = 0x0FFFu;
    uint16_t adc_max = 0u;
    int16_t per_min = 0;
    int16_t per_max = 0;
    uint32_t fft_raw_max = 0u;
    uint16_t fft_disp_max = 0u;

    (void)memset(frame, 0, sizeof(*frame));
    frame->magic         = SCREEN_FRAME_MAGIC;
    frame->version       = SCREEN_FRAME_VERSION;
    frame->byte_len      = sizeof(ScreenDataFrame);
    frame->sequence      = seq;
    frame->frequency_mhz = meas_valid ? (freq_hz * 1000u) : 0u;
    frame->vpp_uv        = meas_valid ? ((uint32_t)vpp_mv * 1000u) : 0u;
    frame->mode          = (uint8_t)SCREEN_MODE_WAVE;
    frame->wave_periods  = wave_periods;
    frame->wave_count    = SCREEN_WAVE_MAX_POINTS;

    /* 统计原始 ADC 码值，便于判断输入是否削顶/削底 */
    for (uint32_t k = 0u; k < adc_count; k++)
    {
        uint16_t c = adc_buf[k] & 0x0FFFU;
        if (c < adc_min) adc_min = c;
        if (c > adc_max) adc_max = c;
    }

    if (model_available
        && screen_fill_from_measurement_model(
            frame,
            &measurement_model,
            wave_periods))
    {
        /* H743 是参数权威；削顶与未削顶帧统一按完整模型绘制。 */
        reconstructed = true;
    }
    else if (period != NULL)
    {
        /* 等效采样已重建出 1 个周期 */
        screen_fill_wave_buffer(frame, period, wave_periods);
        if (ComputePeriodSpectrum(period, raw_spectrum, display_spectrum))
        {
            for (i = 0u; i < (ES_PERIOD_POINTS / 2u); i++)
            {
                frame->fft[i] = display_spectrum[i];
            }
            frame->fft_count = ES_PERIOD_POINTS / 2u;
            screen_extract_harmonics(frame, raw_spectrum, fs_hz, freq_hz, vpp_mv);

            for (i = 0u; i < ES_PERIOD_POINTS; i++)
            {
                if (period[i] < per_min) per_min = period[i];
                if (period[i] > per_max) per_max = period[i];
            }
            for (i = 0u; i < (ES_PERIOD_POINTS / 2u); i++)
            {
                if (raw_spectrum[i] > fft_raw_max) fft_raw_max = raw_spectrum[i];
                if (display_spectrum[i] > fft_disp_max) fft_disp_max = display_spectrum[i];
            }

            if (screen_should_sine_fit(frame))
            {
                screen_sine_fit_period(period, s_period, ES_PERIOD_POINTS);
                screen_fill_wave_buffer(frame, s_period, wave_periods);
            }
        }
        reconstructed = true;
    }
    else if (meas_valid && (freq_hz != 0u))
    {
        /* 普通采样模式下，用 H7 捎带的基频从 raw ADC 重建 1 个周期 */
        static int16_t s_signed_buf[AD9226_SAMPLE_COUNT];

        for (uint32_t k = 0u; k < adc_count; k++)
        {
            int32_t v = (int32_t)(adc_buf[k] & 0x0FFFU) - 2048L;
            if (v < INT16_MIN) v = INT16_MIN;
            else if (v > INT16_MAX) v = INT16_MAX;
            s_signed_buf[k] = (int16_t)v;
        }

        /* 每周期采样点数 >=10 时，普通采样已能稳定重建，优先使用；
         * 点数太少时才依赖等效采样。重建失败时用合成正弦保底。 */
        {
            float samples_per_period = (float)fs_hz / (float)freq_hz;
            bool use_eq = (samples_per_period < 10.0f);

            if (use_eq)
            {
                if ((divider >= ES_TIMER_DIVIDER_MIN) && (divider <= ES_TIMER_DIVIDER_MAX) &&
                    EsReconstructPeriod(s_signed_buf, freq_hz, divider, s_period))
                {
                    reconstructed = true;
                }
                else if (NormalReconstructPeriod(s_signed_buf, adc_count, freq_hz, fs_hz, s_period))
                {
                    reconstructed = true;
                }
            }
            else
            {
                if (NormalReconstructPeriod(s_signed_buf, adc_count, freq_hz, fs_hz, s_period))
                {
                    reconstructed = true;
                }
                else if ((divider >= ES_TIMER_DIVIDER_MIN) && (divider <= ES_TIMER_DIVIDER_MAX) &&
                         EsReconstructPeriod(s_signed_buf, freq_hz, divider, s_period))
                {
                    reconstructed = true;
                }
            }

            if (!reconstructed)
            {
                screen_synth_sine_period(s_period, freq_hz, adc_min, adc_max);
                reconstructed = true;
                printf("[SCREEN] synth sine fallback (f0=%lu Hz, fs=%lu Hz, div=%u)\r\n",
                       (unsigned long)freq_hz, (unsigned long)fs_hz, (unsigned)divider);
            }
        }

        if (reconstructed)
        {
            screen_fill_wave_buffer(frame, s_period, wave_periods);
            if (ComputePeriodSpectrum(s_period, raw_spectrum, display_spectrum))
            {
                for (i = 0u; i < (ES_PERIOD_POINTS / 2u); i++)
                {
                    frame->fft[i] = display_spectrum[i];
                }
                frame->fft_count = ES_PERIOD_POINTS / 2u;
                screen_extract_harmonics(frame, raw_spectrum, fs_hz, freq_hz, vpp_mv);

                for (i = 0u; i < ES_PERIOD_POINTS; i++)
                {
                    if (s_period[i] < per_min) per_min = s_period[i];
                    if (s_period[i] > per_max) per_max = s_period[i];
                }
                for (i = 0u; i < (ES_PERIOD_POINTS / 2u); i++)
                {
                    if (raw_spectrum[i] > fft_raw_max) fft_raw_max = raw_spectrum[i];
                    if (display_spectrum[i] > fft_disp_max) fft_disp_max = display_spectrum[i];
                }

                if (screen_should_sine_fit(frame))
                {
                    screen_sine_fit_period(s_period, s_period, ES_PERIOD_POINTS);
                    screen_fill_wave_buffer(frame, s_period, wave_periods);
                }
            }
        }
    }

    if (!reconstructed)
    {
        /* 回退:直接抽取 raw ADC,保证无 H7 或重建失败时仍有波形 */
        if ((adc_buf != NULL) && (adc_count > 1u))
        {
            for (i = 0u; i < SCREEN_WAVE_MAX_POINTS; i++)
            {
                uint32_t idx = ((uint32_t)i * (adc_count - 1u)) / (SCREEN_WAVE_MAX_POINTS - 1u);
                frame->wave[i] = adc_buf[idx] & 0x0FFFU;
            }
        }
    }

    /* 周期性打印当前帧信息，便于排查频率/重建/按键问题 */
    {
        static uint32_t s_diag_cnt = 0u;
        if (++s_diag_cnt >= 30u)
        {
            s_diag_cnt = 0u;
            const char *src;
            if (period != NULL) src = "eq";
            else if (reconstructed) src = "reconstruct";
            else src = "raw";
            printf("[SCREEN] seq=%lu src=%s fs=%lu div=%u f0=%lu vpp=%u pts=%u per=%u adc=[%u,%u] per=[%d,%d] fft=[raw_max=%lu,disp_max=%u]\r\n",
                   (unsigned long)seq, src,
                   (unsigned long)fs_hz, (unsigned)divider,
                   (unsigned long)freq_hz, (unsigned)vpp_mv,
                   (unsigned)frame->wave_count,
                   (unsigned)frame->wave_periods,
                   (unsigned)adc_min, (unsigned)adc_max,
                   (int)per_min, (int)per_max,
                   (unsigned long)fft_raw_max, (unsigned)fft_disp_max);

            if ((adc_min == 0u) || (adc_max >= 0x0FFEu))
            {
                printf("[SCREEN] WARNING: raw ADC frame clipped (adc=[%u,%u])\r\n",
                       (unsigned)adc_min, (unsigned)adc_max);
            }
        }
    }

    frame->checksum = 0u;  /* 当前 screen_app 不校验 checksum */
}

/* 统计一帧 min/max/mean/std (单点 RMS 噪声) */
static void frame_stats(const uint16_t *buf, uint32_t n,
                        uint16_t *out_min, uint16_t *out_max,
                        uint32_t *out_mean, float *out_std)
{
    uint16_t mn = 0xFFFFu, mx = 0;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; ++i) {
        uint16_t c = buf[i] & 0x0FFFu;
        sum += c;
        if (c < mn) mn = c;
        if (c > mx) mx = c;
    }
    float mean = (float)sum / (float)n;
    *out_min = mn;
    *out_max = mx;
    *out_mean = (uint32_t)(mean + 0.5f);
    /* 第二遍:方差 → std (单点 RMS 噪声, 单位 code) */
    float var = 0.0f;
    for (uint32_t i = 0; i < n; ++i) {
        float d = (float)(buf[i] & 0x0FFFu) - mean;
        var += d * d;
    }
    var /= (float)n;
    *out_std = sqrtf(var);
}

static void dump_calibration_csv(const uint16_t *buf)
{
    const uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim1);
    const uint32_t psc = htim1.Instance->PSC;
    const uint32_t actual_fs_hz = (ES_TIMER_CLOCK_HZ / (psc + 1U)) / (arr + 1U);
    uint32_t sum = 0U;
    uint16_t minimum = 0x0fffU;
    uint16_t maximum = 0U;
    uint32_t bit_ones[12] = {0U};
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i) {
        const uint16_t code = buf[i] & 0x0fffU;
        sum += code;
        if (code < minimum) minimum = code;
        if (code > maximum) maximum = code;
        for (uint32_t bit = 0U; bit < 12U; ++bit) {
            if ((code & (1U << bit)) != 0U) ++bit_ones[bit];
        }
    }
    ++g_calibration_seq;
    printf("[CAL12 BEGIN] seq=%lu count=%lu bits=12 fs_hz=%lu arr=%lu psc=%lu min=%u max=%u pp=%u sum=%lu\r\n",
           (unsigned long)g_calibration_seq, (unsigned long)AD9226_SAMPLE_COUNT,
           (unsigned long)actual_fs_hz, (unsigned long)arr, (unsigned long)psc,
           minimum, maximum, (unsigned)(maximum - minimum), (unsigned long)sum);
    printf("# bit_ones_lsb_to_msb");
    for (uint32_t bit = 0U; bit < 12U; ++bit) printf(",%lu", (unsigned long)bit_ones[bit]);
    printf("\r\nindex,code\r\n");
    for (uint32_t i = 0U; i < AD9226_SAMPLE_COUNT; ++i) {
        printf("%lu,%u\r\n", (unsigned long)i, (unsigned)(buf[i] & 0x0fffU));
    }
    printf("[CAL12 END] seq=%lu\r\n", (unsigned long)g_calibration_seq);
}

/* CSV 打印前 dump_n 点: 索引,原始码值,微伏(旧标定；新标定使用c命令的原始码) */
static void dump_frame_csv(const uint16_t *buf, uint32_t dump_n)
{
    if (dump_n > AD9226_SAMPLE_COUNT) dump_n = AD9226_SAMPLE_COUNT;
    printf("=== frame dump (first %lu points) ===\r\n", (unsigned long)dump_n);
    for (uint32_t i = 0; i < dump_n; ++i) {
        uint16_t code = buf[i] & 0x0FFFu;
        int32_t uV = ((int32_t)code - 2055) * 5236;
        printf("%lu,%u,%ld\r\n", (unsigned long)i, code, (long)uV);
    }
    printf("=== end dump ===\r\n");
}

/* Binary 镜像:把 4096 点 AD9226 样本同步到 UART(给 PC 端 viewer 看 SPI 真实数据)
 *
 * 帧格式 (小端, STM32F4 本身就是 LE,直接 memcpy 即可):
 *   offset 0   : STX     = 0xAA 0x55          (2B)
 *   offset 2   : seq     = uint32_t           (4B)  帧序号
 *   offset 6   : fs_hz   = uint32_t           (4B)  采样率
 *   offset 10  : n       = uint16_t = 4096    (2B)
 *   offset 12  : codes   = uint16_t[n] LE     (n*2B) raw 12-bit code
 *   offset 12+n*2 : ETX   = 0x55 0xAA         (2B)
 *
 * 4206B/帧 @ 921600 baud = 71ms/帧 → F407→H7 14 fps (够调试)
 *                       115200 baud = 712ms/帧 → 1.4 fps (太慢)
 *
 * 用 HAL_UART_Transmit 直送二进制,不走 printf,不走 newlib-nano buffer,
 * 字节序和 STM32 SPI 寄存器发给 H7 的字节序完全一致 (STM32 SPI 8bit MSB-first,
 * 16bit sample 拆 2 byte: 高字节先发,等价于 little-endian 字节序)
 */
#define DUMP_BIN_STX_HI  0xAA
#define DUMP_BIN_STX_LO  0x55
#define DUMP_BIN_ETX_HI  0x55
#define DUMP_BIN_ETX_LO  0xAA
#define DUMP_BIN_HEADER  12
#define DUMP_BIN_FOOTER  2

static void dump_frame_binary(const uint16_t *buf, uint32_t n,
                              uint32_t seq, uint32_t fs_hz)
{
    if (n == 0 || buf == NULL) return;
    /* 12B header 一次发,内部小端直接 memcpy (F407 LE) */
    uint8_t hdr[DUMP_BIN_HEADER];
    hdr[0] = DUMP_BIN_STX_HI;
    hdr[1] = DUMP_BIN_STX_LO;
    memcpy(&hdr[2],  &seq,   4);
    memcpy(&hdr[6],  &fs_hz, 4);
    memcpy(&hdr[10], &n,     2);
    HAL_UART_Transmit(&huart1, hdr, DUMP_BIN_HEADER, HAL_MAX_DELAY);

    /* 8KB raw samples 直送 (Cortex-M4 LE,buf 内存布局就是 LE uint16)
     * 关键: 必须 & 0x0FFF! AD9226 12-bit 数据落在 s_adc_buf[i] 低 12 位,
     * 高 4 位是 PF12-PF15 的电平(被 DMA 一起读到 IDR 里了,不是 ADC 数据)
     * 不 mask 会让 viewer 看到 mean=26000 (0x6<<12) 而不是 ~2055
     * 用 static 8KB 缓冲避免 8KB 局部数组爆 F407 栈 (默认栈 8KB)
     * 同时 dump_frame_binary 在 main loop 调,不在 ISR 调,static 不会有竞争 */
    static uint16_t s_masked[AD9226_SAMPLE_COUNT];
    for (uint32_t i = 0; i < n; ++i) {
        s_masked[i] = buf[i] & 0x0FFFu;
    }
    HAL_UART_Transmit(&huart1, (const uint8_t *)s_masked, n * 2U, HAL_MAX_DELAY);

    /* 2B ETX */
    uint8_t ftr[DUMP_BIN_FOOTER] = { DUMP_BIN_ETX_HI, DUMP_BIN_ETX_LO };
    HAL_UART_Transmit(&huart1, ftr, DUMP_BIN_FOOTER, HAL_MAX_DELAY);
}

/**
 * @brief  UART CSV 输出等效采样重建后的波形,用于 PC viewer 调试或观察
 * @param  cycles : 1 = 输出 256 点(1 个基频周期),3 = 输出 768 点(3 个周期)
 * @note   必须在 EsReconstructPeriod() 之后调用,否则会读到旧数据或 0
 */
static void dump_eq_waveform(uint16_t cycles)
{
    if (cycles != 1U && cycles != 3U) {
        cycles = 1U;
    }
    static int16_t s_wfbuf[ES_MAX_DISPLAY_POINTS];
    EsWaveformInfo info = {0};
    uint16_t n = EsWaveform_Copy(s_wfbuf, ES_MAX_DISPLAY_POINTS, cycles, &info);
    if (n == 0U) {
        printf("[EQ] waveform not ready\r\n");
        return;
    }

    printf("[EQ] seq=%lu f0=%lu Hz Fs=%lu Hz divider=%u cycles=%u points=%u\r\n",
           (unsigned long)info.sequence,
           (unsigned long)info.fundamental_hz,
           (unsigned long)info.sample_rate_hz,
           (unsigned)AD9226_GetEqDivider(),
           (unsigned)cycles,
           (unsigned)n);

    for (uint16_t i = 0; i < n; i++) {
        /* 输出 signed LSB 值,与原始 int16_t 同量纲(2048=0) */
        printf("%d\r\n", (int)s_wfbuf[i]);
    }
    printf("[EQ] end\r\n");
}
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
#ifdef USE_FULL_ASSERT
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
