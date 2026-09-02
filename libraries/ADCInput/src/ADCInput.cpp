#include "ADCInput.h"

#include <string.h>

extern "C" {
#include "ch32h417.h"
#include "ch32h4_gpio.h"
#include "ch32h4_irq.h"
#include "ch32h4_rcc.h"
#include "ch32h4_timer.h"
}

/* TIM3's update event drives TRGO, which is one of the ADC's regular-group
 * trigger sources on this part. The full list is TIM1 CC1/CC2/CC3, TIM2 CC2,
 * TIM3 TRGO, TIM4 CC4 and TIM8 TRGO -- and TRGO is the one that needs no
 * output-compare channel set up, so it costs a timer and not a timer plus a
 * pin. The timer is taken through the core's registry, so a sketch that also
 * wants TIM3 for a Servo or for tone() gets a refusal rather than two drivers
 * reprogramming one timer underneath each other. */
#define ADC_TIMER_ID 3

/* Reference manual table 10-2: ADC1 is request 120. DMA1 channels 1-3 are
 * SPI's, 4 and 5 are I2S's; 7 is free. */
#define ADC_DMA_REQ_ADC1    120
#define ADC_DMA_CHANNEL     DMA1_Channel7
#define ADC_DMA_MUX_CHANNEL DMA_MuxChannel7
#define ADC_DMA_IRQN        DMA1_Channel7_IRQn
#define ADC_DMA_FLAGS       0x0F000000u   /* channel 7 nibble */
#define ADC_DMA_HT_FLAG     0x04000000u

/* One scan per half, up to eight channels. Small on purpose: the interrupt
 * arrives once per half, so a big buffer means a long wait before a sketch
 * sees the first sample, and this path exists for sketches that want to
 * process audio as it arrives. */
#define DMA_SCANS_PER_HALF 32
#define DMA_HALF_SAMPLES   (DMA_SCANS_PER_HALF * 8)
#define DMA_BUF_SAMPLES    (DMA_HALF_SAMPLES * 2)

/* In the shared region with the other DMA buffers, for the same reasons. */
static uint16_t s_dma_buf[DMA_BUF_SAMPLES]
    __attribute__((aligned(4), section(".sdram")));

static ADCInput *s_instance = nullptr;

ADCInput::ADCInput(pin_size_t p0, pin_size_t p1, pin_size_t p2, pin_size_t p3,
                   pin_size_t p4, pin_size_t p5, pin_size_t p6, pin_size_t p7) {
    setPins(p0, p1, p2, p3, p4, p5, p6, p7);
}

ADCInput::~ADCInput() {
    end();
    free(_ring);
}

bool ADCInput::setPins(pin_size_t p0, pin_size_t p1, pin_size_t p2,
                       pin_size_t p3, pin_size_t p4, pin_size_t p5,
                       pin_size_t p6, pin_size_t p7) {
    if (_running) {
        return false;
    }
    const pin_size_t given[8] = { p0, p1, p2, p3, p4, p5, p6, p7 };
    _nchannels = 0;
    for (int i = 0; i < 8; i++) {
        if (given[i] == 0xFF) {
            continue;
        }
        if (given[i] >= PINS_COUNT) {
            return false;
        }
        const uint8_t ch = g_pins[given[i]].adc_channel;
        if (ch == 0xFF) {
            /* A pin with no ADC input. Refusing beats sampling a channel the
             * sketch did not name and reporting it as that pin's value. */
            return false;
        }
        _pins[_nchannels] = given[i];
        _channels[_nchannels] = ch;
        _nchannels++;
    }
    return _nchannels > 0;
}

bool ADCInput::setFrequency(int rate) {
    if (_running || rate <= 0) {
        return false;
    }
    _rate = (uint32_t)rate;
    return true;
}

bool ADCInput::setBuffer(size_t samples) {
    if (_running) {
        return false;
    }
    if (samples < DMA_HALF_SAMPLES * 2) {
        samples = DMA_HALF_SAMPLES * 2;
    }
    samples = ((samples + DMA_HALF_SAMPLES - 1) / DMA_HALF_SAMPLES)
              * DMA_HALF_SAMPLES;
    free(_ring);
    _ring = (uint16_t *)malloc(samples * sizeof(uint16_t));
    _ring_size = _ring ? samples : 0;
    _head = _tail = 0;
    return _ring != nullptr;
}

/* ---- ring --------------------------------------------------------------- */

size_t ADCInput::ringUsed() const {
    const size_t h = _head, t = _tail;
    return (h >= t) ? (h - t) : (_ring_size - t + h);
}

size_t ADCInput::ringFree() const {
    return _ring_size - ringUsed() - 1;
}

/* ---- hardware ----------------------------------------------------------- */

bool ADCInput::configureTimer() {
    if (!ch32h4_timer_claim(ADC_TIMER_ID, CH32H4_TIMER_ADC)) {
        return false;
    }
    ch32h4_timer_clock_enable(ADC_TIMER_ID);
    ch32h4_timer_reset(ADC_TIMER_ID);

    TIM_TypeDef *dev = ch32h4_timer_dev(ADC_TIMER_ID);
    const uint32_t clk = ch32h4_timer_input_clock(ADC_TIMER_ID);

    /* Pick the smallest prescaler that puts the reload inside 16 bits, so the
     * reload carries as much resolution as possible and the delivered rate is
     * as close as the hardware allows. */
    uint32_t total = clk / _rate;
    if (total == 0) {
        return false;
    }
    uint32_t prescaler = 1;
    while (total / prescaler > 0x10000u) {
        prescaler++;
    }
    uint32_t reload = total / prescaler;
    if (reload < 2) {
        reload = 2;
    }
    if (reload > 0x10000u) {
        reload = 0x10000u;
    }
    _actual_rate = clk / (prescaler * reload);

    TIM_TimeBaseInitTypeDef t = {0};
    t.TIM_Prescaler = (uint16_t)(prescaler - 1u);
    t.TIM_CounterMode = TIM_CounterMode_Up;
    t.TIM_Period = (uint16_t)(reload - 1u);
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(dev, &t);

    /* Update as TRGO. Nothing is routed to a pin, so no output compare
     * channel and no GPIO are involved. */
    TIM_SelectOutputTrigger(dev, TIM_TRGOSource_Update);
    return true;
}

bool ADCInput::configureADC() {
    ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_ADC1);
    ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_ADC1);

    for (uint8_t i = 0; i < _nchannels; i++) {
        const ch32h4_pin_t *p = &g_pins[_pins[i]];
        /* Analog mode: no AF mux, and the mode register's analog encoding
         * disconnects the digital input buffer. */
        ch32h4_pin_af(p->port, p->bit, CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);
    }

    ADC_InitTypeDef a = {0};
    a.ADC_Mode = ADC_Mode_Independent;
    /* Scan mode, so one trigger converts the whole channel list rather than
     * only the first. Without it a multi-pin capture silently samples pin 0
     * over and over. */
    a.ADC_ScanConvMode = (_nchannels > 1) ? ENABLE : DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;
    a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
    a.ADC_DataAlign = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel = _nchannels;
    ADC_Init(ADC1, &a);

    for (uint8_t i = 0; i < _nchannels; i++) {
        /* CyclesMode3 rather than the slowest: this path is rate-paced, and
         * the sample window plus the 12.5-cycle conversion has to fit inside
         * one trigger period times the channel count. The slowest setting is
         * 239.5 cycles, which at a 12.5 MHz ADCCLK is 20 us a channel and
         * would cap an eight-channel scan at about 6 kHz. */
        ADC_RegularChannelConfig(ADC1, _channels[i], i + 1,
                                 ADC_SampleTime_CyclesMode3);
    }

    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    ADC_Cmd(ADC1, ENABLE);
    return true;
}

void ADCInput::configureDMA() {
    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    (void)RCC->HBPCENR;

    DMA_Cmd(ADC_DMA_CHANNEL, DISABLE);
    DMA1->INTFCR = ADC_DMA_FLAGS;

    /* Whole scans per half, so a half-complete interrupt never delivers a
     * partial scan -- a partial scan would put every later sample on the
     * wrong channel for the rest of the capture. */
    const uint32_t per_half = DMA_SCANS_PER_HALF * _nchannels;

    DMA_InitTypeDef init = {0};
    init.DMA_PeripheralBaseAddr = (uint32_t)&ADC1->RDATAR;
    init.DMA_Memory0BaseAddr = (uint32_t)s_dma_buf;
    init.DMA_DIR = DMA_DIR_PeripheralSRC;
    init.DMA_BufferSize = per_half * 2u;
    init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_HalfWord;
    init.DMA_MemoryDataSize = DMA_MemoryDataSize_HalfWord;
    init.DMA_Mode = DMA_Mode_Circular;
    init.DMA_Priority = DMA_Priority_High;
    init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(ADC_DMA_CHANNEL, &init);

    DMA_MuxChannelConfig(ADC_DMA_MUX_CHANNEL, ADC_DMA_REQ_ADC1);
    DMA_ITConfig(ADC_DMA_CHANNEL, DMA_IT_TC | DMA_IT_HT, ENABLE);
    NVIC_SetPriority(ADC_DMA_IRQN, 2);
    NVIC_EnableIRQ(ADC_DMA_IRQN);
    DMA_Cmd(ADC_DMA_CHANNEL, ENABLE);
}

/* ---- interrupt ---------------------------------------------------------- */

void ADCInput::_dmaHalfComplete(bool second_half) {
    const uint32_t per_half = DMA_SCANS_PER_HALF * _nchannels;
    const uint16_t *src = s_dma_buf + (second_half ? per_half : 0);

    if (ringFree() < per_half) {
        /* Drop the whole half. See the header: a partial scan desynchronises
         * the channels for everything after it. */
        _overflows++;
        return;
    }
    size_t h = _head;
    for (uint32_t i = 0; i < per_half; i++) {
        _ring[h] = src[i];
        h = (h + 1 == _ring_size) ? 0 : h + 1;
    }
    _head = h;
}

extern "C" void CH32H4_IRQ_HANDLER(DMA1_Channel7_IRQHandler);
extern "C" void DMA1_Channel7_IRQHandler(void) {
    const uint32_t status = DMA1->INTFR & ADC_DMA_FLAGS;
    DMA1->INTFCR = ADC_DMA_FLAGS;
    if (s_instance) {
        /* Half-complete means the FIRST half is full; transfer-complete means
         * the second. */
        s_instance->_dmaHalfComplete((status & ADC_DMA_HT_FLAG) == 0);
    }
}

/* ---- lifecycle ---------------------------------------------------------- */

bool ADCInput::begin(long sampleRate) {
    return setFrequency((int)sampleRate) && begin();
}

bool ADCInput::begin() {
    if (_running) {
        return true;
    }
    if (_nchannels == 0) {
        return false;
    }
    if (!_ring && !setBuffer(DMA_HALF_SAMPLES * 4)) {
        return false;
    }
    _head = _tail = 0;
    _overflows = 0;
    memset(s_dma_buf, 0, sizeof(s_dma_buf));

    if (!configureTimer()) {
        return false;
    }
    if (!configureADC()) {
        ch32h4_timer_release(ADC_TIMER_ID, CH32H4_TIMER_ADC);
        return false;
    }

    s_instance = this;
    configureDMA();
    _running = true;

    /* The timer starts last: everything downstream of it has to be ready
     * before the first trigger, or the first scan lands in a DMA that is not
     * armed and the channel order is off by one for the whole capture. */
    TIM_Cmd(ch32h4_timer_dev(ADC_TIMER_ID), ENABLE);
    return true;
}

void ADCInput::end() {
    if (!_running) {
        return;
    }
    TIM_Cmd(ch32h4_timer_dev(ADC_TIMER_ID), DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    DMA_Cmd(ADC_DMA_CHANNEL, DISABLE);
    NVIC_DisableIRQ(ADC_DMA_IRQN);
    ch32h4_timer_release(ADC_TIMER_ID, CH32H4_TIMER_ADC);
    s_instance = nullptr;
    _running = false;
}

uint32_t ADCInput::getOverflows() {
    const uint32_t n = _overflows;
    _overflows = 0;
    return n;
}

/* ---- Stream ------------------------------------------------------------- */

int ADCInput::available() {
    return _running ? (int)ringUsed() : 0;
}

int ADCInput::read() {
    if (!_running || ringUsed() == 0) {
        return -1;
    }
    const uint16_t v = _ring[_tail];
    _tail = (_tail + 1 == _ring_size) ? 0 : _tail + 1;
    return (int)v;
}

int ADCInput::peek() {
    if (!_running || ringUsed() == 0) {
        return -1;
    }
    return (int)_ring[_tail];
}

void ADCInput::flush() {
    /* Discard what has been captured. On an input stream that is what flush
     * can usefully mean -- there is nothing outstanding to push. */
    _tail = _head;
}

size_t ADCInput::read(uint16_t *buf, size_t count) {
    if (!_running || !buf) {
        return 0;
    }
    const size_t have = ringUsed();
    if (count > have) {
        count = have;
    }
    size_t t = _tail;
    for (size_t i = 0; i < count; i++) {
        buf[i] = _ring[t];
        t = (t + 1 == _ring_size) ? 0 : t + 1;
    }
    _tail = t;
    return count;
}
