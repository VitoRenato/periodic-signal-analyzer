#ifndef TEST_MAINCHAIN_HAL_H
#define TEST_MAINCHAIN_HAL_H

#include <stdint.h>

typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef struct { uint32_t PSC; } TIM_TypeDef;
typedef struct { uint32_t CR1; } SPI_TypeDef;
typedef struct { uint32_t CR1; } USART_TypeDef;
typedef struct { uint32_t IDR; } GPIO_TypeDef;
typedef struct { uint32_t unused; } DMA_Channel_TypeDef;

typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef struct { uint32_t ClockType, SYSCLKSource, AHBCLKDivider, APB1CLKDivider, APB2CLKDivider; } RCC_ClkInitTypeDef;
typedef struct { uint32_t PLLState, PLLSource, PLLM, PLLN, PLLP, PLLQ; } RCC_PLLInitTypeDef;
typedef struct { uint32_t OscillatorType, HSEState; RCC_PLLInitTypeDef PLL; } RCC_OscInitTypeDef;
typedef struct { uint32_t ClockSource; } TIM_ClockConfigTypeDef;
typedef struct { uint32_t MasterOutputTrigger, MasterSlaveMode; } TIM_MasterConfigTypeDef;
typedef struct { uint32_t OCMode, Pulse, OCPolarity, OCNPolarity, OCFastMode, OCIdleState, OCNIdleState; } TIM_OC_InitTypeDef;
typedef struct { uint32_t OffStateRunMode, OffStateIDLEMode, LockLevel, DeadTime, BreakState, BreakPolarity, AutomaticOutput; } TIM_BreakDeadTimeConfigTypeDef;
typedef struct { uint32_t Prescaler, CounterMode, Period, ClockDivision, RepetitionCounter, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
typedef struct { uint32_t Direction, Mode, DataSize, CLKPolarity, CLKPhase, NSS, BaudRatePrescaler, FirstBit, TIMode, CRCCalculation, CRCPolynomial; } SPI_InitTypeDef;
typedef struct { SPI_TypeDef *Instance; SPI_InitTypeDef Init; uint32_t ErrorCode; struct { uint32_t unused; } *hdmarx; } SPI_HandleTypeDef;
typedef struct { uint32_t BaudRate, WordLength, StopBits, Parity, Mode, HwFlowCtl, OverSampling; } UART_InitTypeDef;
typedef struct { USART_TypeDef *Instance; UART_InitTypeDef Init; } UART_HandleTypeDef;
typedef struct { uint32_t Channel, Direction, PeriphInc, MemInc, PeriphDataAlignment, MemDataAlignment, Mode, Priority, FIFOMode; } DMA_InitTypeDef;
typedef struct { DMA_Channel_TypeDef *Instance; DMA_InitTypeDef Init; } DMA_HandleTypeDef;

#define GPIOA ((GPIO_TypeDef *)0)
#define GPIOC ((GPIO_TypeDef *)0)
#define GPIOE ((GPIO_TypeDef *)0)
#define GPIOF ((GPIO_TypeDef *)0)
#define GPIOH ((GPIO_TypeDef *)0)
#define SPI1 ((SPI_TypeDef *)0)
extern TIM_TypeDef test_tim1;
#define TIM1 (&test_tim1)
#define USART1 ((USART_TypeDef *)0)
#define USART2 ((USART_TypeDef *)0)
#define DMA1_Stream5 ((DMA_Channel_TypeDef *)0)
#define DMA1_Stream6 ((DMA_Channel_TypeDef *)0)
#define DMA2_Stream5 ((DMA_Channel_TypeDef *)0)
#define USART2_IRQn 1
#define DMA1_Stream6_IRQn 2
#define DMA2_Stream5_IRQn 3
#define M_PI 3.14159265358979323846
#define __disable_irq() ((void)0)

#define GPIO_PIN_0 1U
#define GPIO_PIN_1 2U
#define GPIO_PIN_2 4U
#define GPIO_PIN_3 8U
#define GPIO_PIN_4 16U
#define GPIO_PIN_5 32U
#define GPIO_PIN_6 64U
#define GPIO_PIN_7 128U
#define GPIO_PIN_8 256U
#define GPIO_PIN_9 512U
#define GPIO_PIN_10 1024U
#define GPIO_PIN_11 2048U
#define GPIO_PIN_12 4096U
#define GPIO_PIN_15 32768U
#define GPIO_PIN_RESET 0U
#define GPIO_PIN_SET 1U
#define GPIO_MODE_INPUT 0U
#define GPIO_MODE_OUTPUT_PP 1U
#define GPIO_MODE_AF_PP 2U
#define GPIO_NOPULL 0U
#define GPIO_PULLUP 1U
#define GPIO_SPEED_FREQ_LOW 0U
#define GPIO_SPEED_FREQ_VERY_HIGH 3U
#define GPIO_AF7_USART2 7U

#define RCC_OSCILLATORTYPE_HSE 1U
#define RCC_HSE_ON 1U
#define RCC_PLL_ON 1U
#define RCC_PLLSOURCE_HSE 1U
#define RCC_PLLP_DIV2 2U
#define RCC_CLOCKTYPE_HCLK 1U
#define RCC_CLOCKTYPE_SYSCLK 2U
#define RCC_CLOCKTYPE_PCLK1 4U
#define RCC_CLOCKTYPE_PCLK2 8U
#define RCC_SYSCLKSOURCE_PLLCLK 1U
#define RCC_SYSCLK_DIV1 0U
#define RCC_HCLK_DIV2 1U
#define RCC_HCLK_DIV4 2U
#define FLASH_LATENCY_5 5U
#define PWR_REGULATOR_VOLTAGE_SCALE1 1U
#define __HAL_RCC_PWR_CLK_ENABLE() ((void)0)
#define __HAL_PWR_VOLTAGESCALING_CONFIG(x) ((void)(x))
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOH_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMA2_CLK_ENABLE() ((void)0)
#define __HAL_RCC_USART2_CLK_ENABLE() ((void)0)
#define __HAL_AFIO_REMAP_SWJ_NOJTAG() ((void)0)
#define __HAL_LINKDMA(h, f, d) ((void)0)

#define TIM_CHANNEL_1 1U
#define TIM_COUNTERMODE_UP 0U
#define TIM_CLOCKDIVISION_DIV1 0U
#define TIM_AUTORELOAD_PRELOAD_ENABLE 1U
#define TIM_CLOCKSOURCE_INTERNAL 0U
#define TIM_TRGO_UPDATE 0U
#define TIM_MASTERSLAVEMODE_ENABLE 1U
#define TIM_OCMODE_PWM1 0U
#define TIM_OCPOLARITY_HIGH 0U
#define TIM_OCNPOLARITY_HIGH 0U
#define TIM_OCFAST_DISABLE 0U
#define TIM_OCIDLESTATE_RESET 0U
#define TIM_OCNIDLESTATE_RESET 0U
#define TIM_OSSR_DISABLE 0U
#define TIM_OSSI_DISABLE 0U
#define TIM_LOCKLEVEL_OFF 0U
#define TIM_BREAK_DISABLE 0U
#define TIM_BREAKPOLARITY_HIGH 0U
#define TIM_AUTOMATICOUTPUT_DISABLE 0U
#define TIM_DMA_UPDATE 1U

#define SPI_MODE_MASTER 0U
#define SPI_DIRECTION_2LINES 0U
#define SPI_DATASIZE_8BIT 0U
#define SPI_POLARITY_LOW 0U
#define SPI_PHASE_1EDGE 0U
#define SPI_NSS_SOFT 0U
#define SPI_BAUDRATEPRESCALER_16 0U
#define SPI_FIRSTBIT_MSB 0U
#define SPI_TIMODE_DISABLE 0U
#define SPI_CRCCALCULATION_DISABLE 0U

#define UART_WORDLENGTH_8B 0U
#define UART_STOPBITS_1 0U
#define UART_PARITY_NONE 0U
#define UART_MODE_TX_RX 0U
#define UART_HWCONTROL_NONE 0U
#define UART_OVERSAMPLING_16 0U
#define DMA_CHANNEL_4 0U
#define DMA_CHANNEL_6 0U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_MEMORY_TO_PERIPH 1U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_PDATAALIGN_HALFWORD 1U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_HALFWORD 1U
#define DMA_NORMAL 0U
#define DMA_PRIORITY_LOW 0U
#define DMA_PRIORITY_HIGH 1U
#define DMA_FIFOMODE_DISABLE 0U
#define HAL_MAX_DELAY 0xffffffffU
#define HAL_DMA_XFER_CPLT_CB_ID 0U
#define __HAL_TIM_GET_AUTORELOAD(t) ((t)->Instance->PSC)

HAL_StatusTypeDef HAL_Init(void);
HAL_StatusTypeDef HAL_RCC_OscConfig(RCC_OscInitTypeDef *s);
HAL_StatusTypeDef HAL_RCC_ClockConfig(RCC_ClkInitTypeDef *s, uint32_t f);
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *s);
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *s);
HAL_StatusTypeDef HAL_TIM_ConfigClockSource(TIM_HandleTypeDef *s, TIM_ClockConfigTypeDef *c);
HAL_StatusTypeDef HAL_TIM_PWM_Init(TIM_HandleTypeDef *s);
HAL_StatusTypeDef HAL_TIMEx_MasterConfigSynchronization(TIM_HandleTypeDef *s, TIM_MasterConfigTypeDef *c);
HAL_StatusTypeDef HAL_TIM_PWM_ConfigChannel(TIM_HandleTypeDef *s, TIM_OC_InitTypeDef *c, uint32_t ch);
HAL_StatusTypeDef HAL_TIMEx_ConfigBreakDeadTime(TIM_HandleTypeDef *s, TIM_BreakDeadTimeConfigTypeDef *c);
HAL_StatusTypeDef HAL_TIM_ConfigClockSource(TIM_HandleTypeDef *s, TIM_ClockConfigTypeDef *c);
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *s);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *s, uint8_t *p, uint16_t n);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *s, const uint8_t *p, uint16_t n, uint32_t t);
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *s);
HAL_StatusTypeDef HAL_DMA_RegisterCallback(DMA_HandleTypeDef *s, uint32_t id, void (*cb)(DMA_HandleTypeDef *));
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g);
void HAL_GPIO_WritePin(GPIO_TypeDef *p, uint16_t pin, uint32_t state);
void HAL_NVIC_SetPriority(uint32_t irq, uint32_t p, uint32_t s);
void HAL_NVIC_EnableIRQ(uint32_t irq);
uint32_t HAL_GetTick(void);

#endif
