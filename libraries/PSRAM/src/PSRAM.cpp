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

void PSRAMClass::mapEnter(void) {
    if (_mapped) { return; }
    /* Wait for idle before touching CCR. The vendor example does this and it
       is not optional: mapEnter() runs straight after an indirect transfer,
       which waits for TC but not for BUSY to fall, and configuring CCR on a
       busy controller wedges it. A wedged controller is not a normal failure
       -- the first AHB read of the window then hangs the CPU forever, with no
       fault and no timeout, and the debug probe cannot halt the core. */
    uint32_t t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_IDLE) == RESET) {
        if (micros() - t0 > 5000) { return; }    /* stay unmapped, not wedged */
    }
    QSPI_ComConfig_InitTypeDef c = {};
    c.QSPI_ComConfig_IMode = QSPI_ComConfig_IMode_1Line;
    c.QSPI_ComConfig_ADMode = QSPI_ComConfig_ADMode_4Line;
    c.QSPI_ComConfig_DMode = QSPI_ComConfig_DMode_4Line;
    c.QSPI_ComConfig_ABMode = QSPI_ComConfig_ABMode_NoAlternateByte;
    c.QSPI_ComConfig_FMode = QSPI_ComConfig_FMode_Memory_Mapped;
    /* SIOO stays disabled. Sending 0xEB once and letting later transactions
       continue without it is measurably wrong on this chip: every word of a
       64 KB burst came back corrupt, and the burst took exactly as long as
       before, so it does not even buy the instruction phase back. Reads are
       already at line rate; there was nothing to win here. */
    c.QSPI_ComConfig_SIOOMode = QSPI_ComConfig_SIOOMode_Disable;
    c.QSPI_ComConfig_ABSize = QSPI_ComConfig_ABSize_8bit;
    c.QSPI_ComConfig_ADSize = QSPI_ComConfig_ADSize_24bit;
    c.QSPI_ComConfig_Ins = CMD_QUAD_READ;
    c.QSPI_ComConfig_DummyCycles = QUAD_READ_DUMMY;
    QSPI_ComConfig_Init(PSRAM_QSPI, &c);
    QSPI_EnableQuad(PSRAM_QSPI, ENABLE);

    /* No timeout counter. It looks like free tCEM insurance -- it drops CE#
       after an idle gap, and this part only refreshes while CE# is high -- but
       the vendor's memory-mapped example does not arm it, and nothing here has
       shown it is safe to. The burst test can turn it on to measure whether it
       is worth having; until that says yes, this matches the one sequence
       known to work. */
    QSPI_Start(PSRAM_QSPI);
    _mapped = true;
}

/* Memory-mapped mode leaves the controller permanently busy, so an indirect
 * transfer cannot simply wait for idle -- it has to abort out first. */
void PSRAMClass::mapExit(void) {
    if (!_mapped) { return; }
    QSPI_TimeoutCounterCmd(PSRAM_QSPI, DISABLE);   /* no-op unless a test armed it */
    QSPI_AbortRequest(PSRAM_QSPI);
    uint32_t t0 = micros();
    while (QSPI_GetFlagStatus(PSRAM_QSPI, QSPI_FLAG_IDLE) == RESET) {
        if (micros() - t0 > 5000) { break; }
    }
    _mapped = false;
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
    mapEnter();                 /* the resting state from here on */
    return true;
}

bool PSRAMClass::end(void) {
    if (!_begun) { return true; }
    mapExit();
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
    if (!_begun || addr >= CAPACITY) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    /* The window, not an indirect transfer. Same bytes, one code path, and no
       mode flip -- reads are the case that must stay cheap. */
    mapEnter();
    if (!_mapped) { return 0; }   /* never touch a window that is not live */
    memcpy(dst, (const void *)(MMAP_BASE + addr), len);
    return len;
}

size_t PSRAMClass::write(uint32_t addr, const void *src, size_t len) {
    if (!_begun || addr >= CAPACITY) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    mapExit();
    const bool ok = xfer(CMD_QUAD_WRITE, addr, true, nullptr,
                         (const uint8_t *)src, (uint32_t)len, 4, 0);
    mapEnter();
    return ok ? len : 0;
}
