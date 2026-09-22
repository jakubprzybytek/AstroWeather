#include "bridge.h"

#include <stdbool.h>

#include "bridge_config.h"
#include "main.h"
#include "st67_mode.h"
#include "usbd_cdc_if.h"

extern UART_HandleTypeDef huart2;

#if ((USB_TO_UART_RING_SIZE & (USB_TO_UART_RING_SIZE - 1u)) != 0u)
#error "USB_TO_UART_RING_SIZE must be a power of two"
#endif

#if ((UART_TO_USB_RING_SIZE & (UART_TO_USB_RING_SIZE - 1u)) != 0u)
#error "UART_TO_USB_RING_SIZE must be a power of two"
#endif

static uint8_t usb_to_uart_data[USB_TO_UART_RING_SIZE];
static volatile uint32_t usb_to_uart_head;
static volatile uint32_t usb_to_uart_tail;

static uint8_t uart_to_usb_data[UART_TO_USB_RING_SIZE];
static volatile uint32_t uart_to_usb_head;
static volatile uint32_t uart_to_usb_tail;

static uint8_t usb_tx_buffer[USB_TX_BUFFER_SIZE];
static volatile bool usb_tx_busy;

static volatile uint32_t usb_to_uart_overflow_count;
static volatile uint32_t uart_to_usb_overflow_count;
static volatile uint32_t uart_hardware_overrun_count;
static volatile uint32_t uart_framing_error_count;
static volatile uint32_t uart_noise_error_count;
static volatile uint32_t uart_parity_error_count;
static volatile uint32_t usb_tx_failure_count;
static volatile uint32_t usb_tx_stale_flush_count;
static volatile uint32_t st67_session_reset_count;

/* Re-running ST67_EnterMode is deferred to the foreground loop; HAL_Delay must not run in USB IRQ context. */
static volatile bool host_dtr_asserted;
static volatile bool st67_reset_pending;

/* Forwarding a short burst can finish in microseconds, faster than the eye can see; stretch the LED pulse. */
#define LED_MIN_ON_MS 60u

/* Bound how long a reset waits for an already-submitted USB IN transfer to drain before proceeding. */
#define USB_TX_DRAIN_TIMEOUT_MS 50u

static volatile uint32_t tx_led_last_activity_tick;
static volatile uint32_t rx_led_last_activity_tick;
static volatile bool tx_led_activity_seen;

void Bridge_SetTxLed(bool on)
{
  HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

void Bridge_SetRxLed(bool on)
{
  HAL_GPIO_WritePin(LED_2_GPIO_Port, LED_2_Pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void bridge_update_tx_led(void)
{
  bool busy = (usb_to_uart_tail != usb_to_uart_head);
  if (busy) {
    tx_led_activity_seen = true;
  }
  if (!tx_led_activity_seen) {
    return; /* Keep CubeMX's initial ON level until the first byte is actually forwarded. */
  }
  Bridge_SetTxLed(busy || ((HAL_GetTick() - tx_led_last_activity_tick) < LED_MIN_ON_MS));
}

static void bridge_update_rx_led(void)
{
  bool busy = usb_tx_busy || (uart_to_usb_tail != uart_to_usb_head);
  Bridge_SetRxLed(busy || ((HAL_GetTick() - rx_led_last_activity_tick) < LED_MIN_ON_MS));
}

static void bridge_update_leds(void)
{
  bridge_update_tx_led();
  bridge_update_rx_led();
}

static void bridge_reset_transport_state(void)
{
  USART_TypeDef *uart = huart2.Instance;
  uint32_t wait_start = HAL_GetTick();

  /* Let an already-submitted USB IN transfer finish so old-session bytes don't linger past this point. */
  while (usb_tx_busy && ((HAL_GetTick() - wait_start) < USB_TX_DRAIN_TIMEOUT_MS)) {
  }
  if (usb_tx_busy) {
    usb_tx_busy = false;
    ++usb_tx_stale_flush_count;
  }

  usb_to_uart_tail = usb_to_uart_head;
  uart_to_usb_tail = uart_to_usb_head;

  while ((uart->ISR & USART_ISR_RXNE_RXFNE) != 0u) {
    (void)uart->RDR;
  }
  __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_OREF | UART_CLEAR_NEF |
                        UART_CLEAR_PEF | UART_CLEAR_FEF);
}

static void bridge_usb_to_uart(void)
{
  while (usb_to_uart_tail != usb_to_uart_head) {
    while ((huart2.Instance->ISR & USART_ISR_TXE_TXFNF) == 0u) {
    }

    huart2.Instance->TDR = usb_to_uart_data[usb_to_uart_tail & (USB_TO_UART_RING_SIZE - 1u)];
    ++usb_to_uart_tail;
  }

  bridge_update_tx_led();
}

static void bridge_uart_to_usb(void)
{
  uint16_t length = 0u;
  uint32_t next_tail;

  if (usb_tx_busy || (uart_to_usb_tail == uart_to_usb_head)) {
    return;
  }

  next_tail = uart_to_usb_tail;
  while ((next_tail != uart_to_usb_head) && (length < USB_TX_BUFFER_SIZE)) {
    usb_tx_buffer[length++] = uart_to_usb_data[next_tail & (UART_TO_USB_RING_SIZE - 1u)];
    ++next_tail;
  }

  Bridge_SetRxLed(true);
  usb_tx_busy = true;
  if (CDC_Transmit_FS(usb_tx_buffer, length) == USBD_OK) {
    uart_to_usb_tail = next_tail;
  } else {
    usb_tx_busy = false;
    ++usb_tx_failure_count;
  }

  bridge_update_rx_led();
}

void Bridge_Init(void)
{
  usb_to_uart_head = 0u;
  usb_to_uart_tail = 0u;
  uart_to_usb_head = 0u;
  uart_to_usb_tail = 0u;
  usb_tx_busy = false;
  usb_to_uart_overflow_count = 0u;
  uart_to_usb_overflow_count = 0u;
  uart_hardware_overrun_count = 0u;
  uart_framing_error_count = 0u;
  uart_noise_error_count = 0u;
  uart_parity_error_count = 0u;
  usb_tx_failure_count = 0u;
  usb_tx_stale_flush_count = 0u;
  st67_session_reset_count = 0u;
  host_dtr_asserted = false;
  st67_reset_pending = false;
  tx_led_activity_seen = false;
  tx_led_last_activity_tick = HAL_GetTick();
  rx_led_last_activity_tick = HAL_GetTick();

#ifdef UART_IT_RXFNE
  __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXFNE);
#else
  huart2.Instance->CR1 |= USART_CR1_RXNEIE_RXFNEIE;
#endif
  HAL_NVIC_SetPriority(USART2_IRQn, 1u, 0u);
  HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void Bridge_Process(void)
{
  if (st67_reset_pending) {
    st67_reset_pending = false;
    ++st67_session_reset_count;
    bridge_reset_transport_state();
    ST67_EnterMode((St67Mode)ST67_DEFAULT_MODE);
  }

  bridge_usb_to_uart();
  bridge_uart_to_usb();
  bridge_update_leds();
}

void Bridge_OnHostConnectionChanged(bool dtr_asserted)
{
  if (dtr_asserted && !host_dtr_asserted) {
    /* Terminal just opened the port: redo ST67 reset so its banner is sent while someone is listening. */
    st67_reset_pending = true;
  }
  host_dtr_asserted = dtr_asserted;
}

void Bridge_OnUsbRx(const uint8_t *data, uint32_t length)
{
  uint32_t index;

  for (index = 0u; index < length; ++index) {
    uint32_t head = usb_to_uart_head;
    if ((head - usb_to_uart_tail) >= USB_TO_UART_RING_SIZE) {
      ++usb_to_uart_overflow_count;
      continue;
    }

    usb_to_uart_data[head & (USB_TO_UART_RING_SIZE - 1u)] = data[index];
    usb_to_uart_head = head + 1u;
  }

  tx_led_last_activity_tick = HAL_GetTick();
  bridge_update_leds();
}

void Bridge_OnUsbTxComplete(void)
{
  usb_tx_busy = false;
  bridge_update_leds();
}

void USART2_IRQHandler(void)
{
  USART_TypeDef *uart = huart2.Instance;

  while ((uart->ISR & USART_ISR_RXNE_RXFNE) != 0u) {
    uint8_t byte = (uint8_t)(uart->RDR & 0xffu);
    uint32_t head = uart_to_usb_head;

    if ((head - uart_to_usb_tail) < UART_TO_USB_RING_SIZE) {
      uart_to_usb_data[head & (UART_TO_USB_RING_SIZE - 1u)] = byte;
      uart_to_usb_head = head + 1u;
    } else {
      ++uart_to_usb_overflow_count;
    }
  }

  if (uart_to_usb_head != uart_to_usb_tail) {
    rx_led_last_activity_tick = HAL_GetTick();
    bridge_update_leds();
  }

  if ((uart->ISR & USART_ISR_ORE) != 0u) {
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    ++uart_hardware_overrun_count;
  }

  if ((uart->ISR & USART_ISR_FE) != 0u) {
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_FEF);
    ++uart_framing_error_count;
  }

  if ((uart->ISR & USART_ISR_NE) != 0u) {
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_NEF);
    ++uart_noise_error_count;
  }

  if ((uart->ISR & USART_ISR_PE) != 0u) {
    __HAL_UART_CLEAR_FLAG(&huart2, UART_CLEAR_PEF);
    ++uart_parity_error_count;
  }
}
