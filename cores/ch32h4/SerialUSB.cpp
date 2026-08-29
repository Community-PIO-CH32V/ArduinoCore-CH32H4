#include "Arduino.h"
#include "ch32h4_usb.h"
#include "tusb.h"

void CH32H4SerialUSB::begin(unsigned long baud, uint16_t config) {
    /* The host decides the line coding on a CDC link; these are accepted and
     * ignored, which is what every other Arduino USB core does. */
    (void)baud;
    (void)config;

    if (!_running) {
        _running = ch32h4_usb_init();
    }
}

void CH32H4SerialUSB::end() {
    flush();
    _running = false;
}

CH32H4SerialUSB::operator bool() {
    ch32h4_usb_task();
    return ch32h4_usb_active() && tud_cdc_connected();
}

uint32_t CH32H4SerialUSB::baud() {
    cdc_line_coding_t coding;
    tud_cdc_get_line_coding(&coding);
    return coding.bit_rate;
}

int CH32H4SerialUSB::available() {
    ch32h4_usb_task();
    return ch32h4_usb_active() ? (int)tud_cdc_available() : 0;
}

int CH32H4SerialUSB::peek() {
    if (!ch32h4_usb_active()) {
        return -1;
    }
    ch32h4_usb_task();
    uint8_t c;
    return tud_cdc_peek(&c) ? (int)c : -1;
}

int CH32H4SerialUSB::read() {
    if (!ch32h4_usb_active()) {
        return -1;
    }
    ch32h4_usb_task();
    return tud_cdc_available() ? (int)tud_cdc_read_char() : -1;
}

void CH32H4SerialUSB::flush() {
    if (!ch32h4_usb_active()) {
        return;
    }
    tud_cdc_write_flush();
    ch32h4_usb_task();
}

size_t CH32H4SerialUSB::write(uint8_t c) {
    return write(&c, 1);
}

size_t CH32H4SerialUSB::write(const uint8_t *buffer, size_t size) {
    if (!ch32h4_usb_active()) {
        return 0;
    }

    size_t written = 0;
    while (written < size) {
        /* Give up rather than block when nobody is listening. A sketch that
         * prints with no host attached would otherwise stall here forever, and
         * "my board hangs when it is not plugged into a PC" is a miserable
         * thing to debug. */
        if (!tud_cdc_connected()) {
            break;
        }

        uint32_t n = tud_cdc_write(buffer + written, (uint32_t)(size - written));
        written += n;

        if (n == 0) {
            /* The FIFO is full: push what is there and let the stack run. */
            tud_cdc_write_flush();
            ch32h4_usb_task();
        }
    }

    tud_cdc_write_flush();
    return written;
}

CH32H4SerialUSB SerialUSB;
