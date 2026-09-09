#include "PSRAM.h"

#include <string.h>

extern "C" {
#include "ch32h4_rcc.h"
}

PSRAMClass PSRAM;

/* Measured, not assumed: these pins are QSPI2 on AF7, all six the same. The
 * vendor's own example uses QSPI1 with the clock on AF9 and the rest on AF10,
 * so that arrangement does not generalise to this bank. */
#define PSRAM_AF        GPIO_AF7
#define PSRAM_QSPI      QSPI2

#define CMD_READ_ID     0x9F
#define CMD_QUAD_READ   0xEB    /* 6 dummy cycles, address and data 4-line */
#define CMD_QUAD_WRITE  0x38    /* 0 dummy cycles */
#define QUAD_READ_DUMMY 6

static void psramPins(void) {
    /* The SDK's GPIO_Init, deliberately, not the core's ch32h4_pin_af(). This
     * part has an STM32F4-style GPIOx->SPEED slew register in addition to the
     * F1-style mode register, and the core helper never writes it -- so a pin
     * configured through the core sits at SPEED's reset value. */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_AFIO | RCC_HB2Periph_GPIOE, ENABLE);
    static const uint8_t src[6] = { GPIO_PinSource10, GPIO_PinSource11,
                                    GPIO_PinSource12, GPIO_PinSource13,
                                    GPIO_PinSource14, GPIO_PinSource15 };
    for (int i = 0; i < 6; i++) {
        GPIO_PinAFConfig(GPIOE, src[i], PSRAM_AF);
        GPIO_InitTypeDef g = {};
        g.GPIO_Pin = (uint16_t)(1u << (10 + i));
        g.GPIO_Speed = GPIO_Speed_Very_High;
        g.GPIO_Mode = GPIO_Mode_AF_PP;
        GPIO_Init(GPIOE, &g);
    }
}

/* Reset QSPI2, in place of the SDK's QSPI_DeInit().
 *
 * DO NOT call QSPI_DeInit() on this part. It resets the wrong peripheral on
 * the wrong bus: QSPI is on HB1, but the SDK passes RCC_HB1Periph_QSPIx to
 * RCC_HB2PeriphResetCmd(). RCC_HB1Periph_QSPI2 is 0x2000, which on HB2 is
 * TIM8 -- so QSPI_DeInit(QSPI2) resets TIM8 and leaves QSPI2 running.
 * (QSPI_DeInit(QSPI1) resets SPI1 for the same reason: 0x1000 is SPI1 there.)
 *
 * That was harmless while every transfer was indirect, because the controller
 * returns to idle on its own. Memory-mapped mode leaves it permanently busy,
 * so without a real reset a second begin() finds a controller that never goes
 * idle and identify() times out. */
static void psramReset(void) {
    RCC_HB1PeriphResetCmd(RCC_HB1Periph_QSPI2, ENABLE);
    RCC_HB1PeriphResetCmd(RCC_HB1Periph_QSPI2, DISABLE);
}

bool PSRAMClass::xfer(uint8_t ins, uint32_t addr, bool hasAddr, uint8_t *rx,
                      const uint8_t *tx, uint32_t len, int lines, int dummy) {
    uint32_t t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_IDLE) == RESET) {
        if (micros() - t0 > 3000) { return false; }
    }
    QSPI_ComConfig_InitTypeDef c = {};
    c.QSPI_ComConfig_IMode = QSPI_ComConfig_IMode_1Line;
    c.QSPI_ComConfig_ADMode = !hasAddr ? QSPI_ComConfig_ADMode_NoAddress
                            : (lines == 4 ? QSPI_ComConfig_ADMode_4Line
                                          : QSPI_ComConfig_ADMode_1Line);
    c.QSPI_ComConfig_DMode = len == 0 ? QSPI_ComConfig_DMode_NoData
                           : (lines == 4 ? QSPI_ComConfig_DMode_4Line
                                         : QSPI_ComConfig_DMode_1Line);
    c.QSPI_ComConfig_ABMode = QSPI_ComConfig_ABMode_NoAlternateByte;
    c.QSPI_ComConfig_FMode = rx ? QSPI_ComConfig_FMode_Indirect_Read
                                : QSPI_ComConfig_FMode_Indirect_Write;
    c.QSPI_ComConfig_SIOOMode = QSPI_ComConfig_SIOOMode_Disable;
    c.QSPI_ComConfig_ABSize = QSPI_ComConfig_ABSize_8bit;
    c.QSPI_ComConfig_ADSize = QSPI_ComConfig_ADSize_24bit;
    c.QSPI_ComConfig_Ins = ins;
    c.QSPI_ComConfig_DummyCycles = dummy;
    QSPI_ComConfig_Init(PSRAM_QSPI, &c);

    /* SIOXEN gates IO2 and IO3 entirely and has no STM32 equivalent. Without
     * it every 4-line phase fails while single-line phases keep working, which
     * reads exactly like a signal-integrity wall and is not one. */
    QSPI_EnableQuad(PSRAM_QSPI, lines == 4 ? ENABLE : DISABLE);

    if (len) { QSPI_SetDataLength(PSRAM_QSPI, len); }
    if (hasAddr) { QSPI_SetAddress(PSRAM_QSPI, addr); }
    QSPI_Start(PSRAM_QSPI);

    uint32_t i = 0;
    t0 = micros();
    while (i < len) {
        if (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_FT)) {
            if (rx) {
                rx[i++] = QSPI_ReceiveData8(PSRAM_QSPI);
            } else {
                QSPI_SendData8(PSRAM_QSPI, tx[i++]);
            }
            /* Reset per byte moved, so a long transfer is not killed by a
               budget sized for a short one. */
            t0 = micros();
        }
        if (micros() - t0 > 20000) {
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_TC) == RESET) {
        if (micros() - t0 > 5000) {
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_FT);
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_TC);
    return true;
}

/* DMA1 channels 1-5 and 7 are taken: DACAudio has 1, SPI 2 and 3, I2S 4 and
 * 5, ADCInput 7. Channel 6 is free. */
#define PSRAM_DMA_CHANNEL     DMA1_Channel6
#define PSRAM_DMA_MUX_CHANNEL DMA_MuxChannel6
#define PSRAM_DMA_FLAG_TC     DMA1_FLAG_TC6

/* MEASURED, not assumed. The vendor's QSPI_FLASH_DMA example gives 71 for
 * QSPI1 and nothing available documents QSPI2; the SDK header defines no
 * request constants at all. A wrong number here fails silently as a transfer
 * that never completes, so it was found by sweeping candidates against real
 * data. See docs/hazards.md. */
#define PSRAM_DMA_REQ         72

/* Staging for transfers DMA cannot do directly. DMA moves whole words to and
 * from memory, so a caller's unaligned pointer, or a length that is not a
 * multiple of four, needs one copy through here. A ragged BYTE COUNT on the
 * wire is fine -- DLR sets it exactly while the DMA supplies a rounded-up word
 * count, which is measured to work -- so only the memory side ever bounces.
 *
 * In DTCM, which DMA1 reads and writes without trouble. */
static uint8_t s_bounce[256] __attribute__((aligned(4)));

bool PSRAMClass::writeDMA(uint32_t addr, const uint8_t *src, uint32_t len,
                          uint32_t req) {
    uint32_t t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_IDLE) == RESET) {
        if (micros() - t0 > 3000) { return false; }
    }
    /* Clear the previous transfer's flags before waiting on this one's. TC is
       sticky, and the previous transfer leaves one set: without this the wait
       below returns instantly on a stale flag, the DMA is disabled before it
       has moved a word, and the whole thing reports success having written
       nothing. That failure is indistinguishable from a wrong DMAMUX request
       number, which is exactly how it wasted a sweep. */
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_TC);
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_FT);
    DMA_ClearFlag(DMA1, PSRAM_DMA_FLAG_TC);

    QSPI_ComConfig_InitTypeDef c = {};
    c.QSPI_ComConfig_IMode = QSPI_ComConfig_IMode_1Line;
    c.QSPI_ComConfig_ADMode = QSPI_ComConfig_ADMode_4Line;
    c.QSPI_ComConfig_DMode = QSPI_ComConfig_DMode_4Line;
    c.QSPI_ComConfig_ABMode = QSPI_ComConfig_ABMode_NoAlternateByte;
    c.QSPI_ComConfig_FMode = QSPI_ComConfig_FMode_Indirect_Write;
    c.QSPI_ComConfig_SIOOMode = QSPI_ComConfig_SIOOMode_Disable;
    c.QSPI_ComConfig_ABSize = QSPI_ComConfig_ABSize_8bit;
    c.QSPI_ComConfig_ADSize = QSPI_ComConfig_ADSize_24bit;
    c.QSPI_ComConfig_Ins = CMD_QUAD_WRITE;
    c.QSPI_ComConfig_DummyCycles = 0;
    QSPI_ComConfig_Init(PSRAM_QSPI, &c);
    /* Address then length then quad, in the vendor example's order. */
    QSPI_SetAddress(PSRAM_QSPI, addr);
    QSPI_SetDataLength(PSRAM_QSPI, len);
    QSPI_EnableQuad(PSRAM_QSPI, ENABLE);

    QSPI_DMACmd(PSRAM_QSPI, ENABLE);

    ch32h4_clock_enable(CH32_BUS_HB, RCC_HBPeriph_DMA1);
    DMA_DeInit(PSRAM_DMA_CHANNEL);
    DMA_InitTypeDef d = {};
    d.DMA_PeripheralBaseAddr = (uint32_t)&PSRAM_QSPI->DR;
    d.DMA_Memory0BaseAddr = (uint32_t)src;
    d.DMA_DIR = DMA_DIR_PeripheralDST;
    d.DMA_BufferSize = (len + 3) / 4;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    d.DMA_Mode = DMA_Mode_Normal;
    d.DMA_Priority = DMA_Priority_VeryHigh;
    d.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(PSRAM_DMA_CHANNEL, &d);
    DMA_MuxChannelConfig(PSRAM_DMA_MUX_CHANNEL, req);
    DMA_Cmd(PSRAM_DMA_CHANNEL, ENABLE);

    /* This part has an explicit START bit; STM32's QUADSPI does not, and the
       transfer simply never begins without it. */
    QSPI_Start(PSRAM_QSPI);

    t0 = micros();
    while (DMA_GetFlagStatus(DMA1, PSRAM_DMA_FLAG_TC) == RESET) {
        if (micros() - t0 > 200000) {
            QSPI_DMACmd(PSRAM_QSPI, DISABLE);
            DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    QSPI_DMACmd(PSRAM_QSPI, DISABLE);

    t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_TC) == RESET) {
        if (micros() - t0 > 200000) {
            DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_FT);
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_TC);
    DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
    return true;
}

/* An indirect quad read straight into memory, bypassing the memory-mapped
 * window entirely.
 *
 * Note the ordering differs from the write path: the vendor starts the QSPI
 * BEFORE enabling the DMA channel here, and after it there. Both orders are
 * copied from QSPI_FLASH_DMA rather than reasoned about. */
bool PSRAMClass::readDMA(uint32_t addr, uint8_t *dst, uint32_t len) {
    uint32_t t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_IDLE) == RESET) {
        if (micros() - t0 > 3000) { return false; }
    }
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_TC);
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_FT);
    DMA_ClearFlag(DMA1, PSRAM_DMA_FLAG_TC);

    QSPI_ComConfig_InitTypeDef c = {};
    c.QSPI_ComConfig_IMode = QSPI_ComConfig_IMode_1Line;
    c.QSPI_ComConfig_ADMode = QSPI_ComConfig_ADMode_4Line;
    c.QSPI_ComConfig_DMode = QSPI_ComConfig_DMode_4Line;
    c.QSPI_ComConfig_ABMode = QSPI_ComConfig_ABMode_NoAlternateByte;
    c.QSPI_ComConfig_FMode = QSPI_ComConfig_FMode_Indirect_Read;
    c.QSPI_ComConfig_SIOOMode = QSPI_ComConfig_SIOOMode_Disable;
    c.QSPI_ComConfig_ABSize = QSPI_ComConfig_ABSize_8bit;
    c.QSPI_ComConfig_ADSize = QSPI_ComConfig_ADSize_24bit;
    c.QSPI_ComConfig_Ins = CMD_QUAD_READ;
    c.QSPI_ComConfig_DummyCycles = QUAD_READ_DUMMY;
    QSPI_ComConfig_Init(PSRAM_QSPI, &c);
    QSPI_SetAddress(PSRAM_QSPI, addr);
    QSPI_SetDataLength(PSRAM_QSPI, len);
    QSPI_EnableQuad(PSRAM_QSPI, ENABLE);

    ch32h4_clock_enable(CH32_BUS_HB, RCC_HBPeriph_DMA1);
    DMA_DeInit(PSRAM_DMA_CHANNEL);
    DMA_InitTypeDef d = {};
    d.DMA_PeripheralBaseAddr = (uint32_t)&PSRAM_QSPI->DR;
    d.DMA_Memory0BaseAddr = (uint32_t)dst;
    d.DMA_DIR = DMA_DIR_PeripheralSRC;
    d.DMA_BufferSize = (len + 3) / 4;
    d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    d.DMA_MemoryInc = DMA_MemoryInc_Enable;
    d.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    d.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    d.DMA_Mode = DMA_Mode_Normal;
    d.DMA_Priority = DMA_Priority_VeryHigh;
    d.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(PSRAM_DMA_CHANNEL, &d);
    DMA_MuxChannelConfig(PSRAM_DMA_MUX_CHANNEL, PSRAM_DMA_REQ);

    QSPI_DMACmd(PSRAM_QSPI, ENABLE);
    QSPI_Start(PSRAM_QSPI);
    DMA_Cmd(PSRAM_DMA_CHANNEL, ENABLE);

    t0 = micros();
    while (DMA_GetFlagStatus(DMA1, PSRAM_DMA_FLAG_TC) == RESET) {
        if (micros() - t0 > 200000) {
            QSPI_DMACmd(PSRAM_QSPI, DISABLE);
            DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_TC) == RESET) {
        if (micros() - t0 > 200000) {
            QSPI_DMACmd(PSRAM_QSPI, DISABLE);
            DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
            QSPI_AbortRequest(PSRAM_QSPI);
            return false;
        }
    }
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_FT);
    QSPI_ClearFlag(PSRAM_QSPI, QSPI_FLAG_TC);
    QSPI_DMACmd(PSRAM_QSPI, DISABLE);
    DMA_Cmd(PSRAM_DMA_CHANNEL, DISABLE);
    return true;
}

bool PSRAMClass::identify(void) {
    uint8_t id[8] = {0};
    /* Single-line, because this is the one command that must work before
       anything about the quad path has been established. */
    if (!xfer(CMD_READ_ID, 0, true, id, nullptr, 8, 1, 0)) {
        return false;
    }
    _mfid = id[0];
    _kgd = id[1];
    memcpy(_eid, id + 2, 6);
    return _mfid == 0x0D && _kgd == 0x5D;
}

bool PSRAMClass::begin(uint32_t clockHz) {
    if (_begun) {
        return false;
    }
    _detected = false;
    if (clockHz == 0) {
        return false;
    }
    /* QSPI divides HCLK, which is 100 MHz. SystemCoreClock is four times that
       on the V5F and would give a prescaler four times too small -- the same
       trap the timer header warns about. There is no RCC_GetHCLKFreq() in this
       SDK; the frequency comes out of the struct RCC_GetClocksFreq() fills. */
    RCC_ClocksTypeDef clocks = {};
    RCC_GetClocksFreq(&clocks);
    const uint32_t src = clocks.HCLK_Frequency;
    if (src == 0) {
        return false;
    }
    uint32_t presc = (src + clockHz - 1) / clockHz;
    if (presc == 0) { presc = 1; }
    if (presc > 256) { return false; }
    presc -= 1;                 /* the register holds divider-1 */

    psramPins();
    ch32h4_clock_enable(CH32_BUS_HB1, RCC_HB1Periph_QSPI2);

    QSPI_Cmd(PSRAM_QSPI, DISABLE);
    psramReset();
    QSPI_InitTypeDef s = {};
    s.QSPI_Prescaler = presc;
    s.QSPI_CKMode = QSPI_CKMode_Mode0;
    s.QSPI_CSHTime = QSPI_CSHTime_8Cycle;
    s.QSPI_FSize = 22;          /* 2^23 bytes = 8 MB */
    s.QSPI_FSelect = QSPI_FSelect_1;
    s.QSPI_DFlash = QSPI_DFlash_Disable;
    QSPI_Init(PSRAM_QSPI, &s);
    QSPI_SetFIFOThreshold(PSRAM_QSPI, 0);
    QSPI_Cmd(PSRAM_QSPI, ENABLE);

    _clock = src / (presc + 1);
    _detected = identify();
    if (!_detected) {
        QSPI_Cmd(PSRAM_QSPI, DISABLE);
        return false;
    }
    _begun = true;
    return true;
}

bool PSRAMClass::end(void) {
    if (!_begun) { return true; }
    QSPI_AbortRequest(PSRAM_QSPI);
    QSPI_Cmd(PSRAM_QSPI, DISABLE);
    _begun = false;
    _clock = 0;
    return true;
}

void PSRAMClass::eid(uint8_t out[6]) const {
    memcpy(out, _eid, 6);
}

size_t PSRAMClass::read(uint32_t addr, void *dst, size_t len) {
    if (!_begun || addr >= CAPACITY || len == 0) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    uint8_t *d = (uint8_t *)dst;

    if (((uintptr_t)d % 4) != 0) {
        /* Unaligned destination: everything goes through the bounce buffer,
           in chunks, so DMA never writes into the caller's buffer directly. */
        size_t done = 0;
        while (done < len) {
            size_t n = len - done;
            if (n > sizeof(s_bounce)) { n = sizeof(s_bounce); }
            /* Rounded up to whole words. Reading a few bytes more than asked
               for is harmless: the device's address space is exactly its
               capacity, so a read off the end wraps, and the extra bytes are
               discarded here rather than handed back. */
            const uint32_t want = (uint32_t)((n + 3u) & ~(size_t)3);
            if (!readDMA(addr + done, s_bounce, want)) { return done; }
            memcpy(d + done, s_bounce, n);
            done += n;
        }
        return done;
    }

    /* Aligned destination: the whole-word part lands straight in the caller's
       buffer with no copy at all, and only a ragged tail is bounced. */
    const size_t head = len & ~(size_t)3;
    if (head && !readDMA(addr, d, (uint32_t)head)) { return 0; }
    const size_t tail = len - head;
    if (tail) {
        if (!readDMA(addr + head, s_bounce, 4)) { return head; }
        memcpy(d + head, s_bounce, tail);
    }
    return len;
}

size_t PSRAMClass::write(uint32_t addr, const void *src, size_t len) {
    if (!_begun || addr >= CAPACITY || len == 0) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    const uint8_t *s = (const uint8_t *)src;

    const uint32_t t0 = micros();
    size_t done = 0;
    if (((uintptr_t)s % 4) != 0) {
        while (done < len) {
            size_t n = len - done;
            if (n > sizeof(s_bounce)) { n = sizeof(s_bounce); }
            memcpy(s_bounce, s + done, n);
            if (!writeDMA(addr + done, s_bounce, (uint32_t)n, PSRAM_DMA_REQ)) {
                break;
            }
            done += n;
        }
    } else {
        /* Aligned source: the whole-word part is sent straight from the
           caller's buffer. The tail is bounced rather than letting DMA read
           up to three bytes past the end of it. */
        const size_t head = len & ~(size_t)3;
        bool ok = true;
        if (head) {
            ok = writeDMA(addr, s, (uint32_t)head, PSRAM_DMA_REQ);
            if (ok) { done = head; }
        }
        const size_t tail = len - head;
        if (ok && tail) {
            memcpy(s_bounce, s + head, tail);
            if (writeDMA(addr + head, s_bounce, (uint32_t)tail,
                         PSRAM_DMA_REQ)) {
                done = len;
            }
        }
    }
    _lastWriteUs = micros() - t0;
    return done;
}
