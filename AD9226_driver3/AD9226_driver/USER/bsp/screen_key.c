#include "screen_key.h"

#include "key_press_detector.h"
#include "main.h"
#include "stm32f4xx_hal.h"

static KeyPressDetector s_detector;
static volatile uint8_t  s_irq_pending;
static volatile uint32_t s_irq_edge_ms;
static uint8_t s_short_press;
static uint8_t s_long_press;

static bool ScreenKey_ReadPressed(void)
{
  return HAL_GPIO_ReadPin(Single_MCU_GPIO_Port, Single_MCU_Pin) == GPIO_PIN_RESET;
}

static void ScreenKey_EnableIrq(void)
{
  /* PA15 -> EXTI15, 下降沿触发 (按键按下时拉低) */
  __HAL_RCC_SYSCFG_CLK_ENABLE();

  /* EXTI15 源选择：EXTICR[3] 的 bits[15:12] = 0000 表示 PA15 */
  MODIFY_REG(SYSCFG->EXTICR[3], SYSCFG_EXTICR4_EXTI15, 0u);

  EXTI->IMR  |= EXTI_IMR_MR15;
  EXTI->EMR  &= ~EXTI_EMR_MR15;
  EXTI->RTSR &= ~EXTI_RTSR_TR15;   /* 不需要上升沿 */
  EXTI->FTSR |= EXTI_FTSR_TR15;    /* 下降沿 */

  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 6u, 0u);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);
}

void ScreenKey_Init(void)
{
  s_irq_pending = 0u;
  s_irq_edge_ms = 0u;
  s_short_press = 0u;
  s_long_press = 0u;
  KeyPressDetector_Init(&s_detector, ScreenKey_ReadPressed(), HAL_GetTick());
  ScreenKey_EnableIrq();
}

void ScreenKey_NotifyIrq(void)
{
  /* 中断里只记录下降沿时间，判定全部在主循环完成。 */
  s_irq_edge_ms = HAL_GetTick();
  s_irq_pending = 1u;
}

void ScreenKey_Task(uint32_t now_ms)
{
  bool edge_seen;
  uint32_t edge_ms;
  KeyEvent event;

  /* 先取走 EXTI 标志再读引脚，避免把正在进行的按下误判成已完成的短按。 */
  __disable_irq();
  edge_seen = (s_irq_pending != 0u);
  edge_ms = s_irq_edge_ms;
  s_irq_pending = 0u;
  __enable_irq();

  event = KeyPressDetector_Update(&s_detector, now_ms, ScreenKey_ReadPressed(), edge_seen, edge_ms);
  if (event == KEY_EVENT_SHORT)
  {
    s_short_press = 1u;
  }
  else if (event == KEY_EVENT_LONG)
  {
    s_long_press = 1u;
  }
}

uint8_t ScreenKey_TakeSinglePress(void)
{
  uint8_t pressed = s_short_press;
  s_short_press = 0u;
  return pressed;
}

uint8_t ScreenKey_TakeLongPress(void)
{
  uint8_t pressed = s_long_press;
  s_long_press = 0u;
  return pressed;
}
