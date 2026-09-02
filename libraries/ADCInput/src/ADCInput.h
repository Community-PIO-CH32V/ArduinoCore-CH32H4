/* ADCInput -- timer-paced ADC sampling into a ring buffer, in the shape
 * arduino-pico's ADCInput uses.
 *
 *     ADCInput adc(A0);
 *     adc.begin(8000);
 *     while (adc.available()) { int16_t s = adc.read(); }
 *
 * A timer's update event drives TRGO, TRGO triggers the ADC, and the DMA
 * moves each result out of RDATAR. The core does nothing at all during a
 * capture, which is the whole point: analogRead() in a loop cannot hold a
 * sample rate, and a sketch that tries gets jitter proportional to whatever
 * else it is doing.
 *
 * Values are 12-bit, zero to 4095, in a uint16_t -- NOT signed audio. A
 * sketch recording from a microphone through a bias network wants
 * `sample - 2048`, and that subtraction is left to the sketch because the
 * bias point depends on the circuit, not on the ADC.
 */
#pragma once

#include <Arduino.h>

class ADCInput : public Stream {
public:
    /* Up to eight pins, sampled in sequence as one scan. Each scan produces
     * one sample per pin, in the order given, so a two-pin capture
     * interleaves them exactly the way a stereo I2S stream does. */
    ADCInput(pin_size_t pin0, pin_size_t pin1 = 0xFF, pin_size_t pin2 = 0xFF,
             pin_size_t pin3 = 0xFF, pin_size_t pin4 = 0xFF,
             pin_size_t pin5 = 0xFF, pin_size_t pin6 = 0xFF,
             pin_size_t pin7 = 0xFF);
    ~ADCInput();

    bool setPins(pin_size_t pin0, pin_size_t pin1 = 0xFF, pin_size_t pin2 = 0xFF,
                 pin_size_t pin3 = 0xFF, pin_size_t pin4 = 0xFF,
                 pin_size_t pin5 = 0xFF, pin_size_t pin6 = 0xFF,
                 pin_size_t pin7 = 0xFF);

    /* Scans per second, not samples: with two pins at 8000 Hz the ring fills
     * at 16000 values a second. */
    bool setFrequency(int rate);

    /* Ring buffer size in samples, rounded up to a multiple of the DMA half. */
    bool setBuffer(size_t samples);

    bool begin(long sampleRate);
    bool begin();
    void end();

    /* What the timer could actually produce. The prescaler and reload are
     * integers, so most rates are approximated. */
    uint32_t actualFrequency() const { return _actual_rate; }

    /* Non-zero once a DMA half arrived with no room for it. Cleared by
     * reading. The whole half is dropped rather than part of it: a partial
     * scan puts every later sample on the wrong channel. */
    uint32_t getOverflows();

    /* Stream. read() returns one 12-bit value, or -1 when the ring is empty
     * -- which is distinguishable from a sample of zero only because a valid
     * sample is never negative. */
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    /* Many samples at once, which is what any real processing wants. Returns
     * how many were copied. */
    size_t read(uint16_t *buf, size_t count);

    /* Print, not supported: this is an input. */
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t *, size_t) override { return 0; }
    int availableForWrite() override { return 0; }

    /* Called from the DMA interrupt. Public because the ISR is extern "C". */
    void _dmaHalfComplete(bool second_half);

private:
    bool configureTimer();
    bool configureADC();
    void configureDMA();

    size_t ringUsed() const;
    size_t ringFree() const;

    uint8_t _channels[8] = {};
    uint8_t _nchannels = 0;
    pin_size_t _pins[8] = {};

    bool _running = false;
    uint32_t _rate = 8000;
    uint32_t _actual_rate = 0;
    uint8_t _timer = 3;

    uint16_t *_ring = nullptr;
    size_t _ring_size = 0;
    volatile size_t _head = 0;
    volatile size_t _tail = 0;
    volatile uint32_t _overflows = 0;
};
