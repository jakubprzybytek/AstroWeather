#include "bridge.h"

#include <stdbool.h>

#include "bridge_config.h"
#include "main.h"
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

static void bridge_usb_to_uart(void)
{
  while (usb_to_uart_tail != usb_to_uart_head) {
    while ((huart2.Instance->ISR & USART_ISR_TXE_TXFNF) == 0u) {
    }

    huart2.Instance->TDR = usb_to_uart_data[usb_to_uart_tail & (USB_TO_UART_RING_SIZE - 1u)];
    ++usb_to_uart_tail;
  }
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

  usb_tx_busy = true;
  if (CDC_Transmit_FS(usb_tx_buffer, length) == USBD_OK) {
    uart_to_usb_tail = next_tail;
  } else {
    usb_tx_busy = false;
  }
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
  bridge_usb_to_uart();
  bridge_uart_to_usb();
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
}

void Bridge_OnUsbTxComplete(void)
{
  usb_tx_busy = false;
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

  if ((uart->ISR & USART_ISR_ORE) != 0u) {
    __HAL_UART_CLEAR_OREFLAG(&huart2);
    ++uart_hardware_overrun_count;
  }
}
