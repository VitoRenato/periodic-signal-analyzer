#include "screen_key.h"

#include "main.h"
#include "stm32f4xx_hal.h"

#define SCREEN_KEY_DEBOUNCE_MS   30u
#define SCREEN_KEY_IRQ_GAP_MS   200u   /* 机械抖动/一次按键的最小间隔 */

static uint8_t  s_stable_level;
static uint8_t  s_last_raw_level;
static uint8_t  s_single_press;
static uint32_t s_last_change_ms;

static volatile uint8_t  s_irq_pending;
static volatile uint32_t s_last_irq_ms;
static uint32_t          s_last_press_ms;

static uint8_t ScreenKey_ReadSingleRaw(void)
{
  return (HAL_GPIO_ReadPin(Single_MCU_GPIO_Port, Single_MCU_Pin) == GPIO_PIN_RESET) ? 1u : 0u;
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
  s_stable_level   = ScreenKey_ReadSingleRaw();
  s_last_raw_level = s_stable_level;
  s_single_press   = 0u;
  s_last_change_ms = 0u;

  s_irq_pending    = 0u;
  s_last_irq_ms    = 0u;
  s_last_press_ms  = 0u;

  ScreenKey_EnableIrq();
}

void ScreenKey_NotifyIrq(void)
{
  uint32_t now = HAL_GetTick();
  if ((now - s_last_irq_ms) >= SCREEN_KEY_IRQ_GAP_MS)
  {
    s_irq_pending = 1u;
    s_last_irq_ms = now;
  }
}

void ScreenKey_Task(uint32_t now_ms)
{
  uint8_t raw = ScreenKey_ReadSingleRaw();

  if (raw != s_last_raw_level)
  {
    s_last_raw_level = raw;
    s_last_change_ms = now_ms;
    return;
  }

  if ((raw != s_stable_level) && ((now_ms - s_last_change_ms) >= SCREEN_KEY_DEBOUNCE_MS))
  {
    s_stable_level = raw;
    if (s_stable_level != 0u)
    {
      s_single_press = 1u;
    }
  }

  /* EXTI 中断捕获：即使主循环被 HMI 长时间阻塞，也能记录按键 */
  if (s_irq_pending != 0u)
  {
    if ((now_ms - s_last_irq_ms) >= SCREEN_KEY_DEBOUNCE_MS)
    {
      if ((now_ms - s_last_press_ms) >= SCREEN_KEY_IRQ_GAP_MS)
      {
        s_single_press  = 1u;
        s_last_press_ms = now_ms;
      }
      s_irq_pending = 0u;
    }
  }
}

uint8_t ScreenKey_TakeSinglePress(void)
{
  uint8_t pressed = s_single_press;
  s_single_press = 0u;
  return pressed;
}
