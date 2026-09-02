/* I2S on the I2S2/I2S3 peripherals, in the shape arduino-pico's I2S uses.
 *
 *     I2S i2s(OUTPUT);
 *     i2s.setBCLK(PIN_I2S_CK);      // WS is BCLK+1 in hardware terms here
 *     i2s.setDATA(PIN_I2S_SD);
 *     i2s.setBitsPerSample(16);
 *     i2s.begin(44100);
 *     i2s.write(sample_l);  i2s.write(sample_r);
 *
 * I2S(0) is I2S2 (on SPI2) and I2S(1) is I2S3 (on SPI3). They are independent:
 * neither is synchronous to the other and either can be used alone.
 *
 * ### Why this and not the SAI
 *
 * The MicroPython port for this silicon drove I2S from the SAI at first,
 * because every I2S2/I2S3 pin sits in the VIO18 domain and that measured
 * 1.2 V -- unusable against a 3.3 V audio device. VIO18 turned out to be a
 * software-set rail, and this core raises it to 3.3 V during boot, so the
 * original objection is void.
 *
 * Two things are better here. The rate divider is I2SDIV[7:0] plus an ODD
 * half-step, against the SAI's 6-bit MCKDIV, so 44.1 kHz lands within 0.16%
 * (about 3 cents) instead of over 1% (about 21 cents). And the clock source is
 * selected per peripheral rather than shared with SYSCLK's own tree.
 */
#pragma once

#include <Arduino.h>

extern "C" {
#include "ch32h417.h"
}

class I2S : public Stream {
public:
    /* OUTPUT for transmit, INPUT for receive. One direction per object: the
     * peripheral is either master-transmit or master-receive, never both. */
    explicit I2S(PinMode direction = OUTPUT, uint8_t id = 0);
    ~I2S();

    /* Pins. The three are fixed per peripheral by the alternate-function mux,
     * so these check rather than choose -- passing a pin the peripheral cannot
     * reach returns false instead of configuring something that will not work.
     * WS is implied by BCLK: they are adjacent and there is no combination
     * where one is available and the other is not. */
    bool setBCLK(pin_size_t pin);
    bool setDATA(pin_size_t pin);
    bool setDOUT(pin_size_t pin) { return setDATA(pin); }
    bool setDIN(pin_size_t pin) { return setDATA(pin); }

    bool setBitsPerSample(int bps);
    bool setFrequency(int rate);
    bool setStereo(bool stereo = true);

    /* The bus always carries two slots. A mono stream is written into both,
     * because a single slot would come out of one channel at half the
     * expected rate. */
    bool setMono(bool mono = true) { return setStereo(!mono); }

    /* Ring buffer size in bytes, rounded up to a multiple of the DMA half.
     * Bigger hides a longer stall in the sketch; smaller lowers latency. */
    bool setBuffer(size_t bytes);

    /* Clock and word-select come from outside. The divider then drives
     * nothing, so there is no rate to choose and none to refuse -- what
     * arrives is whatever the master sends. */
    bool setSlave(bool slave = true);

    bool begin(long sampleRate);
    bool begin();
    bool end();

    /* What the divider could actually produce. I2SDIV is an integer with one
     * half-step, so most rates are approximated; a sketch generating tones
     * needs the real number, not the requested one. */
    uint32_t actualFrequency() const { return _actual_rate; }

    /* The reachable range, for the bit depth currently set.
     *
     * This matters more here than on most parts. The divider is
     * 2*I2SDIV+ODD with I2SDIV at most 255, so the largest division is 511 --
     * and it divides SYSCLK, which is 400 MHz. In 16-bit mode a frame is 32
     * bus clocks, so the SLOWEST reachable rate is 400e6/(32*511), about
     * 24.5 kHz; in 32-bit mode a frame is 64 clocks and it is about 12.2 kHz.
     *
     * So 44.1 and 48 kHz are comfortable and 8, 16 and 22.05 kHz are simply
     * not available in 16-bit mode. The I2S clock source is selectable
     * (RCC_I2S2CLKSource) but the alternative is the PLL, which is faster
     * still, so there is nothing to switch to. begin() returns false rather
     * than clocking a rate that was never asked for. */
    uint32_t minimumFrequency() const;
    uint32_t maximumFrequency() const;

    /* Non-zero once the DMA has had to send silence (transmit) or drop a
     * buffer (receive). Cleared by reading. Silence on underflow rather than
     * repeating the last buffer is deliberate: a repeated buffer sounds like
     * working audio with a stutter, where a gap sounds like what it is. */
    uint32_t getUnderflows();

    /* Stream */
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;

    /* Print. write() blocks until the sample is queued, up to the stream
     * timeout; availableForWrite() is how a sketch avoids blocking. */
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buf, size_t size) override;
    int availableForWrite() override;
    using Print::write;

    /* Samples rather than bytes, which is what audio code has. Both channels
     * of a stereo frame in one call. */
    size_t write(int16_t left, int16_t right);
    size_t write(int32_t left, int32_t right);
    bool read(int16_t *left, int16_t *right);
    bool read(int32_t *left, int32_t *right);

    /* Called from the DMA interrupt. Public because the ISR is extern "C". */
    void _dmaHalfComplete(bool second_half);

private:
    uint32_t frameBits() const;
    bool configureClock();
    void configurePins();
    void configureDMA();
    void configurePeripheral();

    size_t ringUsed() const;
    size_t ringFree() const;
    void ringPush(const uint8_t *src, size_t n);
    void ringPop(uint8_t *dst, size_t n);

    uint8_t _id;
    bool _rx;
    bool _running = false;
    bool _slave = false;
    uint8_t _bits = 16;
    bool _stereo = true;
    uint32_t _rate = 44100;
    uint32_t _actual_rate = 0;

    pin_size_t _pin_bclk = 0xFF;
    pin_size_t _pin_data = 0xFF;

    SPI_TypeDef *_spi = nullptr;
    DMA_Channel_TypeDef *_dma = nullptr;
    uint32_t _dma_flags = 0;
    IRQn_Type _dma_irqn = (IRQn_Type)0;

    uint8_t *_ring = nullptr;
    size_t _ring_size = 0;
    volatile size_t _head = 0;
    volatile size_t _tail = 0;
    volatile uint32_t _underflows = 0;
};
