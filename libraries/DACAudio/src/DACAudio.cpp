#include "DACAudio.h"

#include <stdlib.h>
#include <string.h>

extern "C" {
#include "ch32h4_timer.h"
#include "ch32h4_rcc.h"
#include "ch32h4_gpio.h"
}

/* Reference manual table 10-2. DAC2's own request (104) is unused: in dual
 * mode the DAC1 request carries both channels. */
#define DAC_DMA_REQ_DAC1    103

/* DMA1 channels 2 and 3 are SPI, 4 and 5 are I2S, 7 is ADCInput. 1 is free. */
#define DAC_DMA_CHANNEL     DMA1_Channel1
#define DAC_DMA_MUX_CHANNEL DMA_MuxChannel1

/* Mid-scale on both channels, packed as the dual holding register wants it.
 * What an idle or starved output sits at: a DAC resting at zero would drive an
 * amplifier hard against one rail. */
#define DAC_SILENCE         0x08000800u

#define DAC_TIMER_ID        6

bool DACAudio::setBuffer(size_t frames) {
    if (_running || frames < MIN_FRAMES) {
        return false;
    }
    _frames = frames;
    return true;
}

bool DACAudio::setStereo(bool stereo) {
    _stereo = stereo;
    return true;
}

bool DACAudio::begin(uint32_t sampleRate) {
    if (_running) {
        return false;
    }
    if (sampleRate < RATE_MIN || sampleRate > RATE_MAX) {
        return false;
    }
    if (!ch32h4_timer_claim(DAC_TIMER_ID, CH32H4_TIMER_AUDIO)) {
        return false;
    }

    _ring = (uint32_t *)malloc(_frames * sizeof(uint32_t));
    if (!_ring) {
        ch32h4_timer_release(DAC_TIMER_ID, CH32H4_TIMER_AUDIO);
        return false;
    }
    for (size_t i = 0; i < _frames; i++) {
        _ring[i] = DAC_SILENCE;
    }
    _rate = sampleRate;
    _underruns = 0;
    _primed = false;
    _queuedAtWrite = 0;
    _wr = 0;

    /* Clear whatever the single-shot path left behind. It keeps its own
     * "started" flag and skips re-initialising a channel it believes is
     * already up; without this, an earlier analogWrite() would leave that flag
     * set, and after end() the next analogWrite() would write to a DAC this
     * code had disabled and silently do nothing. */
    ch32h4_dac_stop(PIN_DAC1);
    ch32h4_dac_stop(PIN_DAC2);

    /* Analog mode on both pads. Anything else leaves the digital input buffer
     * connected to a pin held near mid-rail, which turns on both halves of the
     * input stage and burns current for as long as the DAC is on. */
    ch32h4_pin_af(g_pins[PIN_DAC1].port, g_pins[PIN_DAC1].bit,
                  CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);
    ch32h4_pin_af(g_pins[PIN_DAC2].port, g_pins[PIN_DAC2].bit,
                  CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);

    /* ch32h4_clock_enable() reads the register back; the bare SDK call does
     * not, and a dropped clock enable presents as a peripheral whose registers
     * all read zero. */
    ch32h4_clock_enable(CH32_BUS_HB1, RCC_HB1Periph_DAC);
    ch32h4_clock_enable(CH32_BUS_HB, RCC_HBPeriph_DMA1);

    DAC_InitTypeDef dac = {};
    dac.DAC_Trigger = DAC_Trigger_T6_TRGO;
    dac.DAC_WaveGeneration = DAC_WaveGeneration_None;
    dac.DAC_LFSRUnmask_TriangleAmplitude = 0;
    dac.DAC_OutputBuffer = DAC_OutputBuffer_Enable;
    DAC_Init(DAC_Channel_1, &dac);
    DAC_Init(DAC_Channel_2, &dac);

    DMA_DeInit(DAC_DMA_CHANNEL);
    DMA_InitTypeDef dma = {};
    dma.DMA_PeripheralBaseAddr = (uint32_t)&DAC->RD12BDHR;
    dma.DMA_Memory0BaseAddr = (uint32_t)_ring;
    dma.DMA_DIR = DMA_DIR_PeripheralDST;
    dma.DMA_BufferSize = (uint16_t)_frames;
    dma.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dma.DMA_Mode = DMA_Mode_Circular;
    dma.DMA_Priority = DMA_Priority_VeryHigh;
    dma.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DAC_DMA_CHANNEL, &dma);
    DMA_MuxChannelConfig(DAC_DMA_MUX_CHANNEL, DAC_DMA_REQ_DAC1);
    DMA_Cmd(DAC_DMA_CHANNEL, ENABLE);

    /* ONLY CHANNEL 1's DMA. In dual mode its single request moves one word
     * into RD12BDHR, which lands in both channels' holding registers. Enabling
     * channel 2's as well fetches a second word per frame and drains the ring
     * at twice the rate -- which sounds like the sample rate being double what
     * was asked for, not like a DMA misconfiguration. */
    DAC_DMACmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_1, ENABLE);
    DAC_Cmd(DAC_Channel_2, ENABLE);

    if (!startTimer(sampleRate)) {
        _running = true;    /* so end() tears down what was just built */
        end();
        return false;
    }

    /* Start writing where the DMA is reading, not at word 0.
     *
     * The ring is pre-filled with silence and the DMA starts consuming it the
     * instant it is enabled, so by the time a sketch gets a turn the reader
     * has already moved past word 0. Leaving the writer behind it would report
     * an almost-full ring and make the first write wait a whole buffer period
     * -- 93 ms at the default size -- before one real sample reached the
     * pins. */
    _wr = dmaPosition();
    _blankedTo = _wr;
    _lastWriteUs = micros();
    _running = true;
    return true;
}

bool DACAudio::startTimer(uint32_t rate) {
    ch32h4_timer_clock_enable(DAC_TIMER_ID);
    ch32h4_timer_reset(DAC_TIMER_ID);

    /* ch32h4_timer_input_clock(), NOT SystemCoreClock. Timers divide HCLK on
     * this part and the V5F runs at four times that, so SystemCoreClock makes
     * every rate come out exactly 4x too slow -- which MicroPython's port hit
     * and this core's timer header warns about in as many words. */
    const uint32_t clk = ch32h4_timer_input_clock(DAC_TIMER_ID);
    if (!clk) {
        return false;
    }
    uint32_t psc = 0;
    while ((clk / (psc + 1)) / rate > 0xFFFFu) {
        psc++;
    }
    uint32_t period = (clk / (psc + 1) + rate / 2) / rate;
    if (period < 2) {
        period = 2;
    }

    TIM_TimeBaseInitTypeDef tb = {};
    tb.TIM_Prescaler = (uint16_t)psc;
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_Period = (uint16_t)(period - 1);
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(TIM6, &tb);

    /* The update event drives TRGO, which is what the DAC trigger listens to.
     * Without this the DACs are configured and never fire. */
    TIM_SelectOutputTrigger(TIM6, TIM_TRGOSource_Update);
    TIM_Cmd(TIM6, ENABLE);
    return true;
}

size_t DACAudio::dmaPosition() const {
    /* CNTR counts down from _frames and reloads, so what has been consumed out
     * of this pass is _frames - CNTR. */
    const uint32_t left = DMA_GetCurrDataCounter(DAC_DMA_CHANNEL);
    if (left == 0 || left > _frames) {
        return 0;
    }
    return _frames - left;
}

size_t DACAudio::freeFrames() const {
    const size_t rd = dmaPosition();
    const size_t used = (_wr + _frames - rd) % _frames;
    /* One held back, so a full ring stays distinguishable from an empty one. */
    return _frames - used - 1;
}

size_t DACAudio::availableFrames() {
    if (!_running) {
        return 0;
    }
    syncWriter();
    return freeFrames();
}

bool DACAudio::end() {
    if (!_running && !_ring) {
        return true;        /* already stopped; see AudioSink.h on the bool */
    }
    _running = false;
    TIM_Cmd(TIM6, DISABLE);
    DMA_Cmd(DAC_DMA_CHANNEL, DISABLE);
    DAC_DMACmd(DAC_Channel_1, DISABLE);

    /* Park both channels at mid-scale rather than at whatever sample the DMA
     * stopped on, so switching off leaves no DC offset on the output. */
    DAC->RD12BDHR = DAC_SILENCE;
    DAC_Cmd(DAC_Channel_1, DISABLE);
    DAC_Cmd(DAC_Channel_2, DISABLE);

    ch32h4_timer_release(DAC_TIMER_ID, CH32H4_TIMER_AUDIO);
    free(_ring);
    _ring = nullptr;
    _rate = 0;
    return true;
}

/* Leave the unwritten part of the ring holding silence.
 *
 * The DMA never stops. It circles the ring whether or not anything has been
 * queued, so whatever sits ahead of the write pointer is what gets played when
 * the producer falls behind. Left alone that is the previous pass's audio, and
 * a stalled stream is then heard as the last fraction of a second repeating --
 * which sounds like a decoder fault rather than what it is. Filled with
 * silence it is heard as a gap: unmistakably a gap, and less alarming.
 *
 * ONLY THE NEWLY FREED WORDS, not the whole free region. The invariant is that
 * everything from the write pointer forward to the read pointer is already
 * silence, and it survives the writer advancing on its own -- writing frames
 * only shrinks that span from the front. It breaks solely when the READER
 * advances, because the words it just played were real audio a moment ago and
 * are now sitting in the free region. So those are the words to blank, and
 * there are only as many of them as the DMA consumed since last time.
 *
 * Blanking the whole free region instead costs a pass over the ring on every
 * write. That was measured here, and it did more than waste time: the cost of
 * the pass is elapsed time, and syncWriter() reads elapsed time to decide
 * whether the ring drained -- so a sketch writing frame by frame made this
 * driver declare an underrun against itself and reset the write pointer after
 * every single frame. Two frames out of 4096 survived. */
void DACAudio::blankFree() {
    const size_t rd = dmaPosition();
    while (_blankedTo != rd) {
        _ring[_blankedTo] = DAC_SILENCE;
        _blankedTo = (_blankedTo + 1 == _frames) ? 0 : _blankedTo + 1;
    }
}

/* Has the DMA drained what was queued, and if so, put the writer back with it.
 *
 * NEITHER QUESTION IS ANSWERABLE FROM THE RING POINTERS. The reader never
 * stops, so when a producer falls behind, the read pointer simply walks past
 * the write pointer -- and (_wr - rd) mod N then goes from a small number
 * straight to nearly N, which is exactly what a almost-full ring looks like.
 * An empty ring and a full one are the same reading a few microseconds apart.
 * Measured twice: in the MicroPython port, where testing for "everything free"
 * caught no underrun at all because it is true only for the instant the two
 * pointers are equal; and here, where freeFrames() alone reported 192 free out
 * of 4096 on an idle stream and a producer that asks before writing would have
 * been stuck there forever.
 *
 * The clock has no such ambiguity. The DMA consumes exactly _rate frames a
 * second whatever the producer is doing, so comparing elapsed time against
 * what was queued says plainly whether it ran dry, and stays right however
 * long the gap was.
 *
 * Having run dry, the writer jumps to the read pointer. Writing where the
 * reader has just passed would put the next frame a whole ring-length away
 * from being heard -- 93 ms of silence at the default size, after a stall that
 * was already audible. */
void DACAudio::syncWriter() {
    if (!_running) {
        return;
    }
    const uint32_t now = micros();
    const uint64_t consumed = ((uint64_t)(now - _lastWriteUs) * _rate) / 1000000u;
    if (consumed < (uint64_t)_queuedAtWrite) {
        return;                 /* the writer is still ahead of the reader */
    }
    /* Only a fault if something was actually queued to lose. An empty ring
     * before playback starts is normal, which is why _primed gates this and
     * not the resynchronisation below. */
    if (_primed) {
        if (consumed > (uint64_t)_queuedAtWrite) {
            _underruns++;
        }
        /* Everything ahead of the reader is now last pass's audio, so silence
         * the lot once. This is the only O(ring) path here, and it runs once
         * per stall rather than once per write -- _primed going false is what
         * stops a stalled stream from paying for it again on every poll. */
        for (size_t i = 0; i < _frames; i++) {
            _ring[i] = DAC_SILENCE;
        }
        _primed = false;
    }
    _wr = dmaPosition();
    _blankedTo = _wr;
    _queuedAtWrite = 0;
    _lastWriteUs = now;
}

size_t DACAudio::writeFrames(const int16_t *interleaved, size_t frames) {
    if (!_running || !interleaved) {
        return 0;
    }
    syncWriter();

    size_t done = 0;
    while (done < frames) {
        const size_t space = freeFrames();
        if (space == 0) {
            break;          /* does not wait; see AudioSink.h */
        }
        size_t n = frames - done;
        if (n > space) {
            n = space;
        }
        for (size_t i = 0; i < n; i++) {
            const int16_t l = interleaved[2 * (done + i)];
            const int16_t r = _stereo ? interleaved[2 * (done + i) + 1] : l;
            /* Signed 16-bit to unsigned 12-bit: shift the range up by half
             * scale, then drop the four low bits the DAC cannot represent. */
            const uint32_t lu = (uint32_t)(l + 32768) >> 4;
            const uint32_t ru = (uint32_t)(r + 32768) >> 4;
            _ring[_wr] = lu | (ru << 16);
            _wr = (_wr + 1 == _frames) ? 0 : _wr + 1;
        }
        done += n;
    }

    blankFree();
    _queuedAtWrite = _frames - freeFrames() - 1;
    _lastWriteUs = micros();
    _primed = true;
    return done;
}

size_t DACAudio::writeFrame(int16_t left, int16_t right) {
    const int16_t frame[2] = { left, right };
    return writeFrames(frame, 1);
}
