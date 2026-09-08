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
    QSPI_DeInit(PSRAM_QSPI);
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
    if (!_begun || addr >= CAPACITY) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    return xfer(CMD_QUAD_READ, addr, true, (uint8_t *)dst, nullptr,
                (uint32_t)len, 4, QUAD_READ_DUMMY) ? len : 0;
}

size_t PSRAMClass::write(uint32_t addr, const void *src, size_t len) {
    if (!_begun || addr >= CAPACITY) { return 0; }
    if (len > CAPACITY - addr) { len = CAPACITY - addr; }
    return xfer(CMD_QUAD_WRITE, addr, true, nullptr, (const uint8_t *)src,
                (uint32_t)len, 4, 0) ? len : 0;
}
