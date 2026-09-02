#include "I2S.h"

#include <string.h>

extern "C" {
#include "ch32h4_irq.h"
#include "ch32h4_rcc.h"
}

/* 512 bytes, halved into the two ping-pong regions the DMA alternates between.
 * At 44.1 kHz stereo 16-bit that is 2.9 ms per full buffer, so about 690
 * interrupts a second -- small enough to hide a stall in the sketch behind and
 * large enough not to spend the core in interrupt entry. */
#define DMA_BUF_BYTES  512
#define DMA_HALF_BYTES (DMA_BUF_BYTES / 2)

/* DMA1 channels 4 and 5. Channels 1-3 belong to SPI and the ADC. */
#define I2S_DMA_A_FLAGS 0x0000F000u   /* channel 4 nibble */
#define I2S_DMA_B_FLAGS 0x000F0000u   /* channel 5 nibble */

/* DMAMUX request numbers, reference manual table 10-2. */
#define REQ_I2S2_TX 65
#define REQ_I2S2_RX 66
#define REQ_I2S3_TX 67
#define REQ_I2S3_RX 68

/* I2SDIV is 8 bits and must be at least 2; ODD adds the half step, so the
 * effective divisor 2*I2SDIV+ODD runs from 4 to 511. */
#define DIV_MIN 2
#define DIV_MAX 255

/* The DMA buffers live in the shared region, not in .bss.
 *
 * DMA1 reaches DTCM as well -- unlike the USB and Ethernet masters -- so this
 * is not strictly required. It is here so that the audio path does not depend
 * on that remaining true, and so the buffers sit alongside the other DMA
 * buffers rather than in the middle of the fast heap. Aligned explicitly: the
 * engine moves half-words, and an odd address faults. */
static uint16_t s_dma_buf[2][DMA_BUF_BYTES / 2]
    __attribute__((aligned(4), section(".sdram")));

static I2S *s_instances[2] = { nullptr, nullptr };

I2S::I2S(PinMode direction, uint8_t id)
    : _id(id > 1 ? 0 : id), _rx(direction == INPUT) {
    /* The variant's defaults, so a sketch that only calls begin() works. */
    _pin_bclk = PIN_I2S_CK;
    _pin_data = PIN_I2S_SD;
}

I2S::~I2S() {
    end();
}

bool I2S::setBCLK(pin_size_t pin) {
    if (_running) {
        return false;
    }
    /* The mux fixes these. Reporting false rather than reconfiguring is the
     * honest answer: a sketch asking for a pin the peripheral cannot drive
     * would otherwise get silence out of a pin it never named. */
    if (pin != PIN_I2S_CK) {
        return false;
    }
    _pin_bclk = pin;
    return true;
}

bool I2S::setDATA(pin_size_t pin) {
    if (_running || pin != PIN_I2S_SD) {
        return false;
    }
    _pin_data = pin;
    return true;
}

bool I2S::setBitsPerSample(int bps) {
    if (_running || (bps != 16 && bps != 32)) {
        return false;
    }
    _bits = (uint8_t)bps;
    return true;
}

bool I2S::setFrequency(int rate) {
    if (_running || rate <= 0) {
        return false;
    }
    _rate = (uint32_t)rate;
    return true;
}

bool I2S::setStereo(bool stereo) {
    if (_running) {
        return false;
    }
    _stereo = stereo;
    return true;
}

bool I2S::setSlave(bool slave) {
    if (_running) {
        return false;
    }
    _slave = slave;
    return true;
}

bool I2S::setBuffer(size_t bytes) {
    if (_running) {
        return false;
    }
    if (bytes < DMA_HALF_BYTES * 2) {
        bytes = DMA_HALF_BYTES * 2;
    }
    /* A multiple of the DMA half, so a half-complete interrupt always has a
     * whole region's worth of room or data and never has to split one. */
    bytes = ((bytes + DMA_HALF_BYTES - 1) / DMA_HALF_BYTES) * DMA_HALF_BYTES;
    free(_ring);
    _ring = (uint8_t *)malloc(bytes);
    _ring_size = _ring ? bytes : 0;
    _head = _tail = 0;
    return _ring != nullptr;
}

/* ---- clock -------------------------------------------------------------- */

/* Bus clocks per frame: two slots of 16 or 32 bits. Receive always captures
 * 32-bit slots regardless of what the sketch asked for. */
uint32_t I2S::frameBits() const {
    return (_rx ? 32u : _bits) == 16u ? 32u : 64u;
}

uint32_t I2S::minimumFrequency() const {
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    return clocks.SYSCLK_Frequency / (frameBits() * (DIV_MAX * 2u + 1u));
}

uint32_t I2S::maximumFrequency() const {
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    return clocks.SYSCLK_Frequency / (frameBits() * (DIV_MIN * 2u));
}

bool I2S::configureClock() {
    RCC_ClocksTypeDef clocks;
    RCC_GetClocksFreq(&clocks);
    const uint32_t clk = clocks.SYSCLK_Frequency;

    const uint32_t base = frameBits();

    /* The nearest whole divisor, then split into the byte and the half step.
     * The SDK's I2S_Init() derives one itself by rounding through a decimal
     * intermediate; this overwrites it so the delivered rate is the best the
     * hardware can do and actualFrequency() describes it exactly. */
    uint32_t n = (clk + (base * _rate) / 2u) / (base * _rate);
    if (n < DIV_MIN * 2u) {
        n = DIV_MIN * 2u;
    }
    if (n > DIV_MAX * 2u + 1u) {
        return false;   /* rate too low for this clock */
    }
    _actual_rate = clk / (base * n);
    _spi->I2SPR = (uint16_t)((n >> 1) | ((n & 1u) ? SPI_I2SPR_ODD : 0u));
    return true;
}

/* ---- pins --------------------------------------------------------------- */

void I2S::configurePins() {
    /* WS is the pin below BCLK on this mux, and the two are never separable.
     * Naming it here rather than making it configurable keeps a sketch from
     * asking for a combination the hardware does not have. */
    const pin_size_t ws = PIN_I2S_WS;

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOB | RCC_HB2Periph_AFIO, ENABLE);
    (void)RCC->HB2PCENR;

    struct { pin_size_t pin; bool input; } pins[] = {
        { _pin_bclk, _slave },
        { ws,        _slave },
        { _pin_data, _rx },
    };

    for (auto &p : pins) {
        GPIO_TypeDef *port = g_pins[p.pin].port;
        const uint8_t bit = g_pins[p.pin].bit;

        GPIO_InitTypeDef init = {0};
        init.GPIO_Pin = (uint16_t)(1u << bit);
        /* Reference manual table 9-6 allows a pull on a receiving SD pin. On
         * this silicon that is wrong: with a pull configured the pad still
         * follows the signal but the peripheral samples nothing, because only
         * the floating-input mode routes the pad inward. Measured on the SAI
         * first, and it is the same trap here. */
        init.GPIO_Mode = p.input ? GPIO_Mode_IN_FLOATING : GPIO_Mode_AF_PP;
        init.GPIO_Speed = GPIO_Speed_Very_High;
        GPIO_Init(port, &init);
        GPIO_PinAFConfig(port, bit, GPIO_AF5);
    }
}

/* ---- DMA ---------------------------------------------------------------- */

void I2S::configureDMA() {
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    (void)RCC->HBPCENR;

    DMA_Cmd(_dma, DISABLE);
    DMA1->INTFCR = _dma_flags;

    DMA_InitTypeDef init = {0};
    init.DMA_PeripheralBaseAddr = (uint32_t)&_spi->DATAR;
    init.DMA_Memory0BaseAddr = (uint32_t)s_dma_buf[_id];
    init.DMA_DIR = _rx ? DMA_DIR_PeripheralSRC : DMA_DIR_PeripheralDST;
    /* The I2S data register is 16 bits wide -- a 32-bit sample is two accesses
     * -- so the DMA moves half-words and the length is in transfers, not
     * bytes. This is the one place the SAI differed: its register was 32 bits
     * and it moved whole words. */
    init.DMA_BufferSize = DMA_BUF_BYTES / 2;
    init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    init.DMA_Mode = DMA_Mode_Circular;
    init.DMA_Priority = DMA_Priority_VeryHigh;
    init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(_dma, &init);

    uint8_t req;
    if (_id == 0) {
        req = _rx ? REQ_I2S2_RX : REQ_I2S2_TX;
        DMA_MuxChannelConfig(DMA_MuxChannel4, req);
    } else {
        req = _rx ? REQ_I2S3_RX : REQ_I2S3_TX;
        DMA_MuxChannelConfig(DMA_MuxChannel5, req);
    }

    DMA_ITConfig(_dma, DMA_IT_TC | DMA_IT_HT, ENABLE);
    NVIC_SetPriority(_dma_irqn, 2);
    NVIC_EnableIRQ(_dma_irqn);
    DMA_Cmd(_dma, ENABLE);
}

void I2S::configurePeripheral() {
    ch32h4_clock_enable(CH32_BUS_HB1,
                        _id == 0 ? RCC_HB1Periph_SPI2 : RCC_HB1Periph_SPI3);

    I2S_Cmd(_spi, DISABLE);

    const uint32_t chbits = _rx ? 32u : _bits;
    I2S_InitTypeDef init = {0};
    if (_slave) {
        init.I2S_Mode = _rx ? I2S_Mode_SlaveRx : I2S_Mode_SlaveTx;
    } else {
        init.I2S_Mode = _rx ? I2S_Mode_MasterRx : I2S_Mode_MasterTx;
    }
    init.I2S_Standard = I2S_Standard_Phillips;
    init.I2S_DataFormat = (chbits == 16) ? I2S_DataFormat_16b
                                         : I2S_DataFormat_32b;
    init.I2S_MCLKOutput = I2S_MCLKOutput_Disable;
    init.I2S_AudioFreq = _rate;
    init.I2S_CPOL = I2S_CPOL_Low;
    I2S_Init(_spi, &init);

    SPI_I2S_DMACmd(_spi, _rx ? SPI_I2S_DMAReq_Rx : SPI_I2S_DMAReq_Tx, ENABLE);
}

/* ---- ring buffer -------------------------------------------------------- */

size_t I2S::ringUsed() const {
    const size_t h = _head, t = _tail;
    return (h >= t) ? (h - t) : (_ring_size - t + h);
}

size_t I2S::ringFree() const {
    /* One byte is given up so full and empty are distinguishable by the
     * indices alone. */
    return _ring_size - ringUsed() - 1;
}

void I2S::ringPush(const uint8_t *src, size_t n) {
    size_t h = _head;
    for (size_t i = 0; i < n; i++) {
        _ring[h] = src[i];
        h = (h + 1 == _ring_size) ? 0 : h + 1;
    }
    _head = h;
}

void I2S::ringPop(uint8_t *dst, size_t n) {
    size_t t = _tail;
    for (size_t i = 0; i < n; i++) {
        dst[i] = _ring[t];
        t = (t + 1 == _ring_size) ? 0 : t + 1;
    }
    _tail = t;
}

/* ---- the interrupt ------------------------------------------------------ */

void I2S::_dmaHalfComplete(bool second_half) {
    uint8_t *half = (uint8_t *)s_dma_buf[_id]
                    + (second_half ? DMA_HALF_BYTES : 0);

    if (_rx) {
        /* Drop the whole half rather than part of it when the ring is full: a
         * partial frame desynchronises left from right for every sample after
         * it, which is far worse than a gap. */
        if (ringFree() >= DMA_HALF_BYTES) {
            ringPush(half, DMA_HALF_BYTES);
        } else {
            _underflows++;
        }
        return;
    }

    if (ringUsed() >= DMA_HALF_BYTES) {
        ringPop(half, DMA_HALF_BYTES);
    } else {
        /* Silence, not the previous contents. A repeated buffer sounds like
         * working audio with a stutter; a gap sounds like what it is. */
        memset(half, 0, DMA_HALF_BYTES);
        _underflows++;
    }
}

static void i2s_dma_irq(uint8_t id) {
    I2S *self = s_instances[id];
    const uint32_t flags = (id == 0) ? I2S_DMA_A_FLAGS : I2S_DMA_B_FLAGS;
    const uint32_t ht = (id == 0) ? 0x00004000u : 0x00040000u;
    const uint32_t status = DMA1->INTFR & flags;

    DMA1->INTFCR = flags;
    if (!self) {
        return;
    }
    /* Half-complete means the FIRST half is free (transmit) or full (receive);
     * transfer-complete means the second. Getting these the wrong way round
     * gives audio that plays but is assembled from the halves in the wrong
     * order, which sounds like distortion rather than like a bug. */
    self->_dmaHalfComplete((status & ht) == 0);
}

extern "C" void CH32H4_IRQ_HANDLER(DMA1_Channel4_IRQHandler);
extern "C" void DMA1_Channel4_IRQHandler(void) { i2s_dma_irq(0); }

extern "C" void CH32H4_IRQ_HANDLER(DMA1_Channel5_IRQHandler);
extern "C" void DMA1_Channel5_IRQHandler(void) { i2s_dma_irq(1); }

/* ---- lifecycle ---------------------------------------------------------- */

bool I2S::begin(long sampleRate) {
    return setFrequency((int)sampleRate) && begin();
}

bool I2S::begin() {
    if (_running) {
        return true;
    }
    if (!_ring && !setBuffer(4096)) {
        return false;
    }
    _head = _tail = 0;
    _underflows = 0;
    memset(s_dma_buf[_id], 0, DMA_BUF_BYTES);

    _spi = (_id == 0) ? SPI2 : SPI3;
    _dma = (_id == 0) ? DMA1_Channel4 : DMA1_Channel5;
    _dma_flags = (_id == 0) ? I2S_DMA_A_FLAGS : I2S_DMA_B_FLAGS;
    _dma_irqn = (_id == 0) ? DMA1_Channel4_IRQn : DMA1_Channel5_IRQn;

    configurePins();
    configurePeripheral();
    if (!_slave && !configureClock()) {
        return false;
    }
    if (_slave) {
        /* Nothing to divide: the bus clock arrives on the pins, and the rate
         * delivered is whatever the master sends. */
        _actual_rate = _rate;
    }

    s_instances[_id] = this;
    configureDMA();
    _running = true;
    I2S_Cmd(_spi, ENABLE);
    return true;
}

bool I2S::end() {
    if (!_running) {
        return false;
    }
    I2S_Cmd(_spi, DISABLE);
    DMA_Cmd(_dma, DISABLE);
    NVIC_DisableIRQ(_dma_irqn);
    s_instances[_id] = nullptr;
    _running = false;
    return true;
}

uint32_t I2S::getUnderflows() {
    const uint32_t n = _underflows;
    _underflows = 0;
    return n;
}

/* ---- Stream and Print --------------------------------------------------- */

int I2S::available() {
    return _running && _rx ? (int)ringUsed() : 0;
}

int I2S::availableForWrite() {
    return _running && !_rx ? (int)ringFree() : 0;
}

int I2S::read() {
    if (!_running || !_rx || ringUsed() == 0) {
        return -1;
    }
    uint8_t b;
    ringPop(&b, 1);
    return b;
}

int I2S::peek() {
    if (!_running || !_rx || ringUsed() == 0) {
        return -1;
    }
    return _ring[_tail];
}

void I2S::flush() {
    /* Wait for what has been queued to reach the wire. On a transmit stream
     * that is what a sketch means by flush; on a receive stream there is
     * nothing outstanding to wait for. */
    if (!_running || _rx) {
        return;
    }
    const uint32_t start = millis();
    while (ringUsed() > 0 && (millis() - start) < getTimeout()) {
        yield();
    }
}

size_t I2S::write(const uint8_t *buf, size_t size) {
    if (!_running || _rx) {
        return 0;
    }
    size_t sent = 0;
    const uint32_t start = millis();
    while (sent < size) {
        size_t room = ringFree();
        if (room == 0) {
            if ((millis() - start) >= getTimeout()) {
                break;
            }
            /* The DMA drains the ring from its interrupt, so this does not
             * need to pump anything -- but yielding keeps USB and the network
             * alive while a sketch streams audio. */
            yield();
            continue;
        }
        size_t n = size - sent;
        if (n > room) {
            n = room;
        }
        ringPush(buf + sent, n);
        sent += n;
    }
    return sent;
}

size_t I2S::write(int16_t left, int16_t right) {
    if (!_stereo) {
        /* Mono goes into both slots: one slot alone comes out of a single
         * channel at half the expected rate. */
        right = left;
    }
    const int16_t frame[2] = { left, right };
    return write((const uint8_t *)frame, sizeof(frame)) / sizeof(int16_t);
}

size_t I2S::write(int32_t left, int32_t right) {
    if (!_stereo) {
        right = left;
    }
    const int32_t frame[2] = { left, right };
    return write((const uint8_t *)frame, sizeof(frame)) / sizeof(int32_t);
}

bool I2S::read(int16_t *left, int16_t *right) {
    if (!_running || !_rx || ringUsed() < 4) {
        return false;
    }
    int16_t frame[2];
    ringPop((uint8_t *)frame, sizeof(frame));
    if (left) {
        *left = frame[0];
    }
    if (right) {
        *right = frame[1];
    }
    return true;
}

bool I2S::read(int32_t *left, int32_t *right) {
    if (!_running || !_rx || ringUsed() < 8) {
        return false;
    }
    /* The half-words of a 32-bit sample arrive in the opposite order to the
     * one they need in memory. The data register is 16 bits wide, so one
     * sample is two transfers, and the bus carries the slot most significant
     * half first -- the DMA lands slot[31:16] first while a little-endian
     * 32-bit value wants it second.
     *
     * Measured rather than reasoned: an INMP441 (24 bits left-justified in a
     * 32-bit slot, so the low byte should barely move) gave 93 distinct low
     * bytes as captured and 2 with the halves exchanged. */
    uint16_t raw[4];
    ringPop((uint8_t *)raw, sizeof(raw));
    if (left) {
        *left = (int32_t)(((uint32_t)raw[0] << 16) | raw[1]);
    }
    if (right) {
        *right = (int32_t)(((uint32_t)raw[2] << 16) | raw[3]);
    }
    return true;
}
