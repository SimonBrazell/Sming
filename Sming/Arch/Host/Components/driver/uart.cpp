/*
 uart.cpp - esp8266 UART HAL

 Copyright (c) 2014 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

 @author 2018 mikee47 <mike@sillyhouse.net>

 Additional features to support flexible transmit buffering and callbacks

 */

#include <driver/uart.h>
#include <driver/SerialBuffer.h>
#include <BitManipulations.h>
#include <Clock.h>
#include <cstring>
#include <esp_system.h>

/*
 * Parameters relating to RX FIFO and buffer thresholds
 *
 * 'headroom' is the number of characters which may be received before a receive overrun
 * condition occurs and data is lost.
 *
 * For the hardware FIFO, data is processed via interrupt so the headroom can be fairly small.
 * The greater the headroom, the more interrupts will be generated thus reducing efficiency.
 */
#define RX_FIFO_FULL_THRESHOLD 120									  ///< UIFF interrupt when FIFO bytes > threshold
#define RX_FIFO_HEADROOM (UART_RX_FIFO_SIZE - RX_FIFO_FULL_THRESHOLD) ///< Chars between UIFF and UIOF
/*
 * Using a buffer, data is typically processed via task callback so requires additional time.
 * This figure is set to a nominal default which should provide robust operation for most situations.
 * It can be adjusted if necessary via the rx_headroom parameter.
*/
#define DEFAULT_RX_HEADROOM (32 - RX_FIFO_HEADROOM)

namespace
{
int s_uart_debug_nr = UART_NO;

// Keep track of interrupt enable state for each UART
uint8_t isrMask;
// Keep a reference to all created UARTS - required because they share an ISR
smg_uart_t* uartInstances[UART_COUNT];

// Registered port callback functions
smg_uart_notify_callback_t notifyCallbacks[UART_COUNT];

/** @brief Invoke a port callback, if one has been registered
 *  @param uart
 *  @param code
 */
void notify(smg_uart_t* uart, smg_uart_notify_code_t code)
{
	auto callback = notifyCallbacks[uart->uart_nr];
	if(callback != nullptr) {
		callback(uart, code);
	}
}

__forceinline bool smg_uart_isr_enabled(uint8_t nr)
{
	return bitRead(isrMask, nr);
}

} // namespace

smg_uart_t* smg_uart_get_uart(uint8_t uart_nr)
{
	return (uart_nr < UART_COUNT) ? uartInstances[uart_nr] : nullptr;
}

uint8_t smg_uart_disable_interrupts()
{
	return isrMask;
}

void smg_uart_restore_interrupts()
{
}

bool smg_uart_set_notify(unsigned uart_nr, smg_uart_notify_callback_t callback)
{
	if(uart_nr >= UART_COUNT) {
		return false;
	}

	notifyCallbacks[uart_nr] = callback;
	return true;
}

void smg_uart_set_callback(smg_uart_t* uart, smg_uart_callback_t callback, void* param)
{
	if(uart != nullptr) {
		uart->callback = nullptr; // In case interrupt fires between setting param and callback
		uart->param = param;
		uart->callback = callback;
	}
}

size_t smg_uart_read(smg_uart_t* uart, void* buffer, size_t size)
{
	if(!smg_uart_rx_enabled(uart) || buffer == nullptr || size == 0) {
		return 0;
	}

	notify(uart, UART_NOTIFY_BEFORE_READ);

	size_t read = 0;

	auto buf = static_cast<uint8_t*>(buffer);

	// First read data from RX buffer if in use
	if(uart->rx_buffer != nullptr) {
		while(read < size && !uart->rx_buffer->isEmpty())
			buf[read++] = uart->rx_buffer->readChar();
	}

	return read;
}

size_t smg_uart_rx_available(smg_uart_t* uart)
{
	if(!smg_uart_rx_enabled(uart)) {
		return 0;
	}

	(void)smg_uart_disable_interrupts();

	size_t avail = 0;

	if(uart->rx_buffer != nullptr) {
		avail += uart->rx_buffer->available();
	}

	smg_uart_restore_interrupts();

	return avail;
}

void smg_uart_start_isr(smg_uart_t* uart)
{
	if(!bitRead(isrMask, uart->uart_nr)) {
		bitSet(isrMask, uart->uart_nr);
	}
}

size_t smg_uart_write(smg_uart_t* uart, const void* buffer, size_t size)
{
	if(!smg_uart_tx_enabled(uart) || buffer == nullptr || size == 0) {
		return 0;
	}

	size_t written = 0;

	auto buf = static_cast<const uint8_t*>(buffer);

	while(written < size) {
		if(uart->tx_buffer != nullptr) {
			while(written < size && uart->tx_buffer->writeChar(buf[written])) {
				++written;
			}
		}

		notify(uart, UART_NOTIFY_AFTER_WRITE);

		if(!bitRead(uart->options, UART_OPT_TXWAIT)) {
			break;
		}
	}

	return written;
}

size_t smg_uart_tx_free(smg_uart_t* uart)
{
	if(!smg_uart_tx_enabled(uart)) {
		return 0;
	}

	(void)smg_uart_disable_interrupts();

	size_t space = 0;
	if(uart->tx_buffer != nullptr) {
		space += uart->tx_buffer->getFreeSpace();
	}

	smg_uart_restore_interrupts();

	return space;
}

void smg_uart_wait_tx_empty(smg_uart_t* uart)
{
	if(!smg_uart_tx_enabled(uart)) {
		return;
	}

	notify(uart, UART_NOTIFY_WAIT_TX);

	if(uart->tx_buffer != nullptr) {
		while(!uart->tx_buffer->isEmpty()) {
			delay(0);
		}
	}
}

void smg_uart_set_break(smg_uart_t* uart, bool state)
{
	(void)uart;
	(void)state;
	// Not implemented
}

uint8_t smg_uart_get_status(smg_uart_t* uart)
{
	// Not implemented
	(void)uart;
	return 0;
}

void smg_uart_flush(smg_uart_t* uart, smg_uart_mode_t mode)
{
	if(uart == nullptr) {
		return;
	}

	bool flushRx = mode != UART_TX_ONLY && uart->mode != UART_TX_ONLY;
	bool flushTx = mode != UART_RX_ONLY && uart->mode != UART_RX_ONLY;

	(void)smg_uart_disable_interrupts();
	if(flushRx && uart->rx_buffer != nullptr) {
		uart->rx_buffer->clear();
	}

	if(flushTx && uart->tx_buffer != nullptr) {
		uart->tx_buffer->clear();
	}

	smg_uart_restore_interrupts();
}

uint32_t smg_uart_set_baudrate_reg(int uart_nr, uint32_t baud_rate)
{
	return baud_rate;
}

uint32_t smg_uart_set_baudrate(smg_uart_t* uart, uint32_t baud_rate)
{
	if(uart == nullptr) {
		return 0;
	}

	baud_rate = smg_uart_set_baudrate_reg(uart->uart_nr, baud_rate);
	// Store the actual baud rate in use
	uart->baud_rate = baud_rate;
	return baud_rate;
}

uint32_t smg_uart_get_baudrate(smg_uart_t* uart)
{
	return (uart == nullptr) ? 0 : uart->baud_rate;
}

smg_uart_t* smg_uart_init_ex(const smg_uart_config_t& cfg)
{
	// Already initialised?
	if(smg_uart_get_uart(cfg.uart_nr) != nullptr) {
		return nullptr;
	}

	if(cfg.uart_nr >= UART_COUNT) {
		return nullptr;
	}

	auto uart = new smg_uart_t;
	if(uart == nullptr) {
		return nullptr;
	}

	memset(uart, 0, sizeof(smg_uart_t));
	uart->uart_nr = cfg.uart_nr;
	uart->mode = cfg.mode;
	uart->options = cfg.options;
	uart->tx_pin = UART_PIN_DEFAULT;
	uart->rx_pin = UART_PIN_DEFAULT;
	uart->rx_headroom = DEFAULT_RX_HEADROOM;

	auto rxBufferSize = cfg.rx_size;
	auto txBufferSize = cfg.tx_size;

	// Virtual uart requires a minimum RAM buffer
	rxBufferSize += UART_RX_FIFO_SIZE;
	txBufferSize += UART_TX_FIFO_SIZE;

	if(smg_uart_rx_enabled(uart) && !smg_uart_realloc_buffer(uart->rx_buffer, rxBufferSize)) {
		delete uart;
		return nullptr;
	}

	if(smg_uart_tx_enabled(uart) && !smg_uart_realloc_buffer(uart->tx_buffer, txBufferSize)) {
		delete uart->rx_buffer;
		delete uart;
		return nullptr;
	}

	// OK, buffers allocated so setup hardware
	smg_uart_detach(cfg.uart_nr);

	smg_uart_set_baudrate(uart, cfg.baudrate);
	smg_uart_set_format(uart, cfg.format);
	smg_uart_flush(uart);
	uartInstances[cfg.uart_nr] = uart;
	smg_uart_start_isr(uart);

	notify(uart, UART_NOTIFY_AFTER_OPEN);

	return uart;
}

void smg_uart_uninit(smg_uart_t* uart)
{
	if(uart == nullptr) {
		return;
	}

	notify(uart, UART_NOTIFY_BEFORE_CLOSE);

	smg_uart_stop_isr(uart);
	// If debug output being sent to this UART, disable it
	if(uart->uart_nr == s_uart_debug_nr) {
		smg_uart_set_debug(UART_NO);
	}

	delete uart->rx_buffer;
	delete uart->tx_buffer;
	delete uart;
}

void smg_uart_set_format(smg_uart_t* uart, smg_uart_format_t format)
{
	// Not implemented
	(void)uart;
	(void)format;
}

bool smg_uart_intr_config(smg_uart_t* uart, const smg_uart_intr_config_t* config)
{
	// Not implemented
	(void)uart;
	(void)config;
	return false;
}

void smg_uart_swap(smg_uart_t* uart, int tx_pin)
{
	(void)uart;
	(void)tx_pin;
	// Not implemented
}

bool smg_uart_set_tx(smg_uart_t* uart, int tx_pin)
{
	if(uart == nullptr) {
		return false;
	}

	uart->tx_pin = tx_pin;
	return true;
}

bool smg_uart_set_pins(smg_uart_t* uart, int tx_pin, int rx_pin)
{
	if(uart == nullptr) {
		return false;
	}

	if(tx_pin != UART_PIN_NO_CHANGE) {
		uart->tx_pin = tx_pin;
	}

	if(rx_pin != UART_PIN_NO_CHANGE) {
		uart->rx_pin = rx_pin;
	}

	return true;
}

void smg_uart_set_debug(int uart_nr)
{
	s_uart_debug_nr = uart_nr;
}

int smg_uart_get_debug()
{
	return s_uart_debug_nr;
}

void smg_uart_detach(int uart_nr)
{
	bitClear(isrMask, uart_nr);
}

void smg_uart_detach_all()
{
	// Not implemented
}
