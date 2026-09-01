/* SD card on the CH32H417's SDMMC controller.
 *
 * Ported from the MicroPython port for this silicon. Every comment about what
 * the hardware actually does was paid for on a bench; none of it is inferred
 * from the reference manual, and several parts contradict what the symmetry of
 * the register set suggests. Change nothing here without a card to test on.
 *
 * The chip has two card controllers. SDIO is the familiar STM32F4-shaped one
 * (POWER/CLKCR/DTIMER/FIFO); SDMMC is WCH's own, does up to 200 MHz, and is
 * the one used here -- it is the better block, and it is the one whose pins
 * this board brings out. Its signals are not on the numbered AF mux: the pads
 * come from an AFIO remap field, which is why the pins below are configured as
 * plain alternate-function push-pull with no GPIO_PinAFConfig call. The three
 * mappings are:
 *
 *              CK     CMD    D0     D1     D2     D3    D4    D5    D6   D7
 *   default    PC12   PD2    PC8    PC9    PC10   PC11  PA14  PA15  PC6  PC7
 *   partial    PD11   PD12   PB13   PC9    PB10   PB11  PA14  PA15  PC6  PC7
 *   full       PC12   PC10   PD0    PD1    PD2    PD3   PD4   PD5   PD6  PD7
 *
 * Only the default mapping is wired up here. The other two are a one-line
 * change (GPIO_PinRemapConfig) plus a pin table, and nothing else in this file
 * depends on which is selected.
 *
 * Every SDMMC pin is on VIO18 rather than VDDIO, so the rail has to be up
 * before a card will answer. At the 1.2 V the chip powers up with, a card sees
 * no valid high level at all and CMD0 never even gets a response;
 * ch32h4_v3f_main() raises it to 3.3 V during boot, and begin() checks rather
 * than letting that failure look like bad wiring.
 *
 * Data moves by the controller's own DMA, always through sd_dma_buf. The
 * caller's buffer is essentially never usable directly: it has to be 16-byte
 * aligned and outside DTCM, and DTCM is where .data, .bss and the fast half of
 * the heap live -- the same limit the USB and Ethernet DMA have. A direct path
 * would be code that never runs.
 */
#include <stdbool.h>
#include <string.h>

#include "Arduino.h"
#include "ch32h417.h"
#include "ch32h4_sdmmc.h"
#include "system_ch32h417.h"

/* Prints every command, its flags and its response. Off in a normal build;
 * the one thing that makes a card that will not identify tractable. */
#ifndef SD_TRACE
#define SD_TRACE 0
#endif

#define SD_BLOCK_SIZE CH32H4_SD_BLOCK_SIZE

/* CMD6 answers with a fixed 64-byte function status block. */
#define SD_SWITCH_STATUS_SIZE 64

/* Pins of the default mapping, in the order the controller uses them. */
#define SD_PORT_CK   GPIOC
#define SD_PIN_CK    GPIO_Pin_12
#define SD_PORT_CMD  GPIOD
#define SD_PIN_CMD   GPIO_Pin_2
#define SD_PORT_D0   GPIOC
#define SD_PIN_D0    GPIO_Pin_8
#define SD_PORT_D1   GPIOC
#define SD_PIN_D1    GPIO_Pin_9
#define SD_PORT_D2   GPIOC
#define SD_PIN_D2    GPIO_Pin_10
#define SD_PORT_D3   GPIOC
#define SD_PIN_D3    GPIO_Pin_11

/* SD commands. ACMDs are sent as CMD55 followed by the command itself. */
enum {
    SD_CMD_GO_IDLE_STATE = 0,
    SD_CMD_ALL_SEND_CID = 2,
    SD_CMD_SEND_RELATIVE_ADDR = 3,
    SD_CMD_SWITCH_FUNC = 6,
    SD_CMD_SELECT_CARD = 7,
    SD_CMD_SEND_IF_COND = 8,
    SD_CMD_SEND_CSD = 9,
    SD_CMD_STOP_TRANSMISSION = 12,
    SD_CMD_SEND_STATUS = 13,
    SD_CMD_SET_BLOCKLEN = 16,
    SD_CMD_READ_SINGLE_BLOCK = 17,
    SD_CMD_READ_MULTIPLE_BLOCK = 18,
    SD_CMD_WRITE_BLOCK = 24,
    SD_CMD_WRITE_MULTIPLE_BLOCK = 25,
    SD_CMD_APP_CMD = 55,
    SD_ACMD_SET_BUS_WIDTH = 6,
    SD_ACMD_SEND_OP_COND = 41,
};

/* Response shapes, as the CMD_SET register wants them: the length in
 * RPTY[9:8], plus whether the hardware should check the CRC and the echoed
 * command index. R3 carries neither -- the OCR response has no CRC and
 * reserved bits where the index goes -- and R2's index field is reserved too,
 * so checking either one rejects a perfectly good response. */
#define SD_RESP_NONE (0x0000)
#define SD_RESP_R1   (0x0200 | 0x0400 | 0x0800)
#define SD_RESP_R1B  (0x0300 | 0x0400 | 0x0800)
#define SD_RESP_R2   (0x0100 | 0x0400)
#define SD_RESP_R3   (0x0200)
#define SD_RESP_R6   SD_RESP_R1
#define SD_RESP_R7   SD_RESP_R1

/* Bits of the R1 card status that mean the command did not do what was asked.
 * Bit 8 (READY_FOR_DATA) and bits 12:9 (CURRENT_STATE) are not errors. */
#define SD_R1_ERROR_MASK (0xFDF98008u)

ch32h4_sd_t ch32h4_sd;

/* Bounce buffer for transfers the DMA cannot do in place, which in practice is
 * all of them: the heap fills its DTCM area long before it touches the shared
 * one, so every buffer a sketch is likely to pass in lives where this
 * controller cannot reach. Eight blocks rather than one, so a bounced
 * multi-block transfer stays a multi-block transfer. */
#define SD_BOUNCE_BLOCKS 8
static uint8_t sd_dma_buf[2][SD_BOUNCE_BLOCKS * SD_BLOCK_SIZE]
    __attribute__((aligned(16), section(".sdram")));
static uint8_t sd_dma_half;

#if SD_TRACE
static uint32_t sd_log[24][3];
static int sd_log_n;

static void sd_log_dump(void) {
    for (int i = 0; i < sd_log_n; i++) {
        Serial1.printf("  CMD%u fg=%04x r=%08x\n", ...);
    }
    sd_log_n = 0;
}
#endif

/* --- controller --- */

/* SDCLK as last programmed, so the inter-command gap can be sized in bus
 * clocks rather than guessed in microseconds. */
static uint32_t sd_clk_hz = 400000;

/* NCC: the SD specification requires at least eight clock cycles between the
 * end of one command's response and the start of the next command, and this
 * controller does not insert them. Without this the card is still releasing
 * the CMD line when the next command starts, and the response that comes back
 * is mangled: CMD55 issued straight after CMD8 reports a response-index error
 * with no CMDDONE at all and the previous command's response still sitting in
 * the register. It reproduces every time at 400 kHz, and disappeared the
 * moment a debug print was added between commands -- which is what pointed at
 * the spacing rather than at the command.
 *
 * Eight cycles is 20 us at the 400 kHz identification clock and 0.4 us at
 * 20 MHz, so this costs nothing once the bus speeds up. */
static void sd_cmd_gap(void) {
    uint32_t us = (8000000u + sd_clk_hz - 1) / sd_clk_hz;
    delayMicroseconds(us < 1 ? 1 : us);
}

/* SDCK = SYSPLL / div in high-speed mode, and SYSPLL / div / 64 in low-speed
 * mode, with div in 2..31. The two ranges do not meet -- at a 400 MHz SYSPLL
 * low mode tops out at 3.1 MHz and high mode bottoms out at 12.9 MHz -- so
 * pick whichever can reach the request, preferring the one that does not
 * overshoot it. Returns the frequency actually programmed. */
static uint32_t sd_set_clock(uint32_t hz) {
    uint32_t src = SystemClock;
    uint32_t div, actual, mode;

    if (hz >= src / 31) {
        div = (src + hz - 1) / hz;
        if (div < 2) {
            div = 2;
        }
        if (div > 31) {
            div = 31;
        }
        mode = SDMMC_CLKMode;
        actual = src / div;
    } else {
        uint32_t low = src / 64;
        div = (low + hz - 1) / hz;
        if (div < 2) {
            div = 2;
        }
        if (div > 31) {
            div = 31;
        }
        mode = 0;
        actual = low / div;
    }

    SDMMC->CLK_DIV = (uint16_t)(mode | div | SDMMC_CLKOE);
    sd_clk_hz = actual;
    return actual;
}

static void sd_pin_af(GPIO_TypeDef *port, uint16_t mask) {
    GPIO_InitTypeDef init = {0};
    init.GPIO_Pin = mask;
    init.GPIO_Mode = GPIO_Mode_AF_PP;
    init.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(port, &init);
}

static void sd_pin_release(GPIO_TypeDef *port, uint16_t mask) {
    GPIO_InitTypeDef init = {0};
    init.GPIO_Pin = mask;
    init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    init.GPIO_Speed = GPIO_Speed_Low;
    GPIO_Init(port, &init);
}

static void sd_controller_reset(uint8_t width) {
    RCC_HBPeriphClockCmd(RCC_HBPeriph_SDMMC, ENABLE);

    /* SWP_TBYP disables SWPMI's internal transceiver, which releases its
     * signals to the GPIO mux. That matters here because SWPMI shares pads
     * with this controller: SWPMI_IO is PC6, and SWP_TX/RX/SUP are PC7, PC8
     * and PC9 -- which are SDMMC D6, D7, D0 and D1.
     *
     * Measured: 1-bit mode passes its whole suite without this, so the
     * transceiver at its reset setting does not actually hold DAT0 on PC8. It
     * is kept anyway because the vendor's SD_GPIO_Init() does it, because
     * 4-bit mode also needs PC9 and there is no 4-bit wiring here to test it
     * on, and because it costs one register write. The clock stays on
     * afterwards rather than being gated again, since nothing says the bit
     * survives its block being gated. */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_SWPMI, ENABLE);
    SWPMI->OR |= (1u << 0);

    RCC_HBPeriphResetCmd(RCC_HBPeriph_SDMMC, ENABLE);
    RCC_HBPeriphResetCmd(RCC_HBPeriph_SDMMC, DISABLE);

    /* CONTROL comes out of reset as 0x0015: ALL_CLR and RST_LGC asserted, one
     * data line. Clearing those two is what takes the controller out of reset,
     * so this write is not merely configuration.
     *
     * NEGSMP samples CMD and data on the falling clock edge, which is what the
     * vendor driver uses at every speed. */
    SDMMC->CONTROL = (uint16_t)((width == 4 ? SDMMC_LW_MASK_0 : 0)
                                | SDMMC_DMAEN | SDMMC_NEGSMP);
    SDMMC->TIMEOUT = 0x0F;
    SDMMC->INT_EN = 0;
    SDMMC->INT_FG = 0xFFFF;
    SDMMC->TRAN_MODE = 0;
    SDMMC->BLOCK_CFG = 0;
}

/* Wait for the card to release DAT0, which it holds low while it is busy
 * programming. Every transfer starts with this. */
static int sd_wait_dat0(void) {
    uint32_t deadline = millis() + 1000;
    while ((SDMMC->STATUS & SDMMC_DAT0STA) == 0) {
        if ((int32_t)(millis() - deadline) > 0) {
            return CH32H4_SD_ETIMEDOUT;
        }
    }
    return 0;
}

/* Send one command and wait for its response.
 *
 * Returns 0, or ETIMEDOUT if the card said nothing, or EIO if the response
 * came back malformed. */
static int sd_cmd(uint8_t idx, uint32_t arg, uint32_t resp) {
    sd_cmd_gap();
    SDMMC->INT_FG = SDMMC_IF_CMDDONE | SDMMC_IF_RE_TMOUT
                    | SDMMC_IF_RECRC_WR | SDMMC_IF_REIDX_ER | SDMMC_IF_DATTMO;

    SDMMC->ARGUMENT = arg;
    SDMMC->CMD_SET = (uint16_t)((idx & SDMMC_CMDIDX_MASK) | resp);

    /* Long enough for an R1b whose card holds DAT0 low through an erase; the
     * hardware's own timeout fires well before this and reports a flag. */
    uint32_t deadline = millis() + 1000;
    uint16_t fg;
    for (;;) {
        fg = SDMMC->INT_FG;
        if (fg & (SDMMC_IF_CMDDONE | SDMMC_IF_RE_TMOUT
                  | SDMMC_IF_RECRC_WR | SDMMC_IF_REIDX_ER | SDMMC_IF_DATTMO)) {
            break;
        }
        if ((int32_t)(millis() - deadline) > 0) {
            return CH32H4_SD_ETIMEDOUT;
        }
    }

#if SD_TRACE
    /* Recorded rather than printed: printing between commands changes the
     * spacing on the bus, which is the very thing worth measuring. */
    if (sd_log_n < (int)(sizeof(sd_log) / sizeof(sd_log[0]))) {
        sd_log[sd_log_n][0] = idx;
        sd_log[sd_log_n][1] = fg;
        sd_log[sd_log_n][2] = SDMMC->RESPONSE3;
        sd_log_n++;
    }
#endif

    if (resp == SD_RESP_NONE) {
        /* Nothing to wait for beyond the command leaving the pin; CMDDONE is
         * still set for it, and a timeout flag here means nothing. */
        return 0;
    }
    if (fg & (SDMMC_IF_RE_TMOUT | SDMMC_IF_DATTMO)) {
        return CH32H4_SD_ETIMEDOUT;
    }
    if (fg & (SDMMC_IF_RECRC_WR | SDMMC_IF_REIDX_ER)) {
        return CH32H4_SD_EIO;
    }
    return 0;
}

/* A 48-bit response lands in RESPONSE3, not RESPONSE0: the registers are
 * numbered from the low end of the 128-bit field and a short response occupies
 * the top of it. Getting this backwards yields plausible-looking zeros from
 * every command, so it is worth stating twice. */
static uint32_t sd_resp_short(void) {
    return SDMMC->RESPONSE3;
}

static void sd_resp_long(uint32_t out[4]) {
    out[0] = SDMMC->RESPONSE3;  /* bits 127:96 */
    out[1] = SDMMC->RESPONSE2;
    out[2] = SDMMC->RESPONSE1;
    out[3] = SDMMC->RESPONSE0;  /* bits 31:0 */
}

/* R1 responses carry the card's status; a command the card refused still
 * answers, so the flags have to be looked at separately from the transport. */
static int sd_cmd_r1(uint8_t idx, uint32_t arg, uint32_t resp) {
    int ret = sd_cmd(idx, arg, resp);
    if (ret != 0) {
        return ret;
    }
    if (sd_resp_short() & SD_R1_ERROR_MASK) {
        return CH32H4_SD_EIO;
    }
    return 0;
}

static int sd_app_cmd(ch32h4_sd_t *self, uint8_t idx, uint32_t arg, uint32_t resp) {
    int ret = sd_cmd_r1(SD_CMD_APP_CMD, (uint32_t)self->rca << 16, SD_RESP_R1);
    if (ret != 0) {
        return ret;
    }
    return sd_cmd(idx, arg, resp);
}

/* --- card bring-up --- */

/* Defined with the rest of the data path below, because it is a data
 * transfer: CMD6 answers on the data lines, not in a response register. */
static bool sd_try_high_speed(void);

static uint32_t sd_capacity_blocks(const uint32_t csd[4]) {
    uint32_t structure = csd[0] >> 30;
    if (structure >= 1) {
        /* CSD version 2.0 and 3.0: C_SIZE is bits 69:48 and counts 512 KB
         * units, less one. */
        uint32_t c_size = ((csd[1] & 0x3F) << 16) | (csd[2] >> 16);
        return (c_size + 1) * 1024;
    }
    /* CSD version 1.0: capacity is (C_SIZE + 1) << (C_SIZE_MULT + 2) blocks of
     * READ_BL_LEN bytes, which this normalises to 512-byte blocks. C_SIZE is
     * bits 73:62, C_SIZE_MULT bits 49:47, READ_BL_LEN bits 83:80. */
    uint32_t read_bl_len = (csd[1] >> 16) & 0x0F;
    uint32_t c_size = ((csd[1] & 0x3FF) << 2) | (csd[2] >> 30);
    uint32_t c_size_mult = (csd[2] >> 15) & 0x07;
    uint32_t blocks = (c_size + 1) << (c_size_mult + 2);
    if (read_bl_len > 9) {
        blocks <<= (read_bl_len - 9);
    }
    return blocks;
}

static int sd_card_identify(ch32h4_sd_t *self) {
    int ret;

    self->rca = 0;
    self->card_type = CH32H4_SD_CARD_NONE;

    /* 400 kHz for identification, as the spec requires, and at least 74 clocks
     * on the bus before the first command. */
    sd_set_clock(400000);
    delay(2);

    ret = sd_cmd(SD_CMD_GO_IDLE_STATE, 0, SD_RESP_NONE);
    if (ret != 0) {
        return ret;
    }
    delay(2);

    /* CMD8 tells a v2 card we can supply 2.7-3.6 V and asks it to echo the
     * check pattern back. Older cards answer nothing at all, which is not an
     * error -- it is how they identify themselves. */
    bool v2 = false;
    if (sd_cmd(SD_CMD_SEND_IF_COND, 0x1AA, SD_RESP_R7) == 0) {
        if ((sd_resp_short() & 0xFFF) != 0x1AA) {
            return CH32H4_SD_EIO;
        }
        v2 = true;
    } else {
        /* A card that ignored CMD8 has left the controller unhappy; start over
         * so ACMD41 sees a clean state. */
        ret = sd_cmd(SD_CMD_GO_IDLE_STATE, 0, SD_RESP_NONE);
        if (ret != 0) {
            return ret;
        }
        delay(2);
    }

    /* ACMD41 until the card finishes its power-up. HCS (bit 30) is only
     * meaningful to a v2 card and asks for block addressing. */
    uint32_t ocr = 0;
    uint32_t deadline = millis() + 1000;
    for (;;) {
        ret = sd_app_cmd(self, SD_ACMD_SEND_OP_COND,
                         (v2 ? 0x40000000u : 0u) | 0x00FF8000u, SD_RESP_R3);
        if (ret != 0) {
            return ret;
        }
        ocr = sd_resp_short();
        if (ocr & 0x80000000u) {
            break;
        }
        if ((int32_t)(millis() - deadline) > 0) {
            return CH32H4_SD_ETIMEDOUT;
        }
        delay(1);
    }
    self->card_type = (ocr & 0x40000000u) ? CH32H4_SD_CARD_SDHC
                                          : CH32H4_SD_CARD_SDSC;

    ret = sd_cmd(SD_CMD_ALL_SEND_CID, 0, SD_RESP_R2);
    if (ret != 0) {
        return ret;
    }
    sd_resp_long(self->cid);

    ret = sd_cmd(SD_CMD_SEND_RELATIVE_ADDR, 0, SD_RESP_R6);
    if (ret != 0) {
        return ret;
    }
    self->rca = (uint16_t)(sd_resp_short() >> 16);
    if (self->rca == 0) {
        return CH32H4_SD_EIO;
    }

    ret = sd_cmd(SD_CMD_SEND_CSD, (uint32_t)self->rca << 16, SD_RESP_R2);
    if (ret != 0) {
        return ret;
    }
    sd_resp_long(self->csd);
    self->block_count = sd_capacity_blocks(self->csd);

    ret = sd_cmd_r1(SD_CMD_SELECT_CARD, (uint32_t)self->rca << 16, SD_RESP_R1B);
    if (ret != 0) {
        return ret;
    }

    if (self->width == 4) {
        ret = sd_app_cmd(self, SD_ACMD_SET_BUS_WIDTH, 2, SD_RESP_R1);
        if (ret != 0) {
            return ret;
        }
        SDMMC->CONTROL |= SDMMC_LW_MASK_0;
    }

    /* Standard-capacity cards address by byte and can be told to use a block
     * length other than 512; high-capacity cards are fixed at 512 and ignore
     * this. Sending it either way keeps the two paths identical below. */
    ret = sd_cmd_r1(SD_CMD_SET_BLOCKLEN, SD_BLOCK_SIZE, SD_RESP_R1);
    if (ret != 0) {
        return ret;
    }

    /* Default speed stops at 25 MHz. Going faster is only legal once the card
     * has been switched into high-speed mode with CMD6, and a card that will
     * not switch has to be held at 25 MHz rather than clocked past its rating
     * and hoped for.
     *
     * The switch is skipped entirely below 25 MHz, where it buys nothing. */
    self->high_speed = false;
    if (self->freq > 25000000) {
        if (sd_try_high_speed()) {
            self->high_speed = true;
        } else {
            self->freq = 25000000;
        }
    }

    self->freq = sd_set_clock(self->freq);
    return 0;
}

/* --- data transfer --- */

/* Wait for the card to leave the programming state after a write. The
 * controller's own R1b handling covers the busy signalling on DAT0, but a
 * multi-block write ends with CMD12 and the card can still be programming when
 * the next command arrives. */
static int sd_wait_ready(ch32h4_sd_t *self) {
    uint32_t deadline = millis() + 1000;
    for (;;) {
        int ret = sd_cmd(SD_CMD_SEND_STATUS, (uint32_t)self->rca << 16,
                         SD_RESP_R1);
        if (ret != 0) {
            return ret;
        }
        uint32_t status = sd_resp_short();
        if (status & SD_R1_ERROR_MASK) {
            return CH32H4_SD_EIO;
        }
        if (status & (1u << 8)) {   /* READY_FOR_DATA */
            return 0;
        }
        if ((int32_t)(millis() - deadline) > 0) {
            return CH32H4_SD_ETIMEDOUT;
        }
    }
}

/* Wait for one data phase to reach a flag worth acting on. Returns the flag
 * word, or 0 if the deadline passed first. */
static uint16_t sd_wait_data_flag(void) {
    uint32_t deadline = millis() + 2000;
    for (;;) {
        uint16_t fg = SDMMC->INT_FG & (SDMMC_IF_TRANDONE | SDMMC_IF_BKGAP
                                       | SDMMC_IF_TRANERR | SDMMC_IF_DATTMO
                                       | SDMMC_IF_FIFO_OV);
        if (fg != 0) {
            return fg;
        }
        if ((int32_t)(millis() - deadline) > 0) {
            return 0;
        }
    }
}

static int sd_data_flag_error(uint16_t fg) {
    if (fg == 0) {
        return CH32H4_SD_ETIMEDOUT;
    }
    if (fg & SDMMC_IF_DATTMO) {
        return CH32H4_SD_ETIMEDOUT;
    }
    if (fg & (SDMMC_IF_TRANERR | SDMMC_IF_FIFO_OV)) {
        return CH32H4_SD_EIO;
    }
    return 0;
}

/* Blocks the controller has transferred so far in this run. */
static uint32_t sd_blocks_done(void) {
    return SDMMC->STATUS & SDMMC_MASK_BLOCK_NUM;
}

/* Arm the data engine: reset it, set the direction, then the geometry, then
 * the address -- and the address write is also the trigger.
 *
 * The zero write to BLOCK_CFG is not idempotent configuration, it is the reset
 * of the block counter, and skipping it leaves a transfer inheriting the
 * previous one's state. */
static void sd_arm(const uint8_t *buf, uint32_t blocksize, uint32_t nblocks,
                   bool write) {
    /* Order the memcpy that filled the buffer against the register writes that
     * hand it to another bus master. This was not what fixed the write path --
     * adding it changed nothing -- but handing a buffer to a DMA engine
     * without a barrier is not something to leave to luck. */
    __asm volatile("fence" ::: "memory");
    SDMMC->BLOCK_CFG = 0;
    SDMMC->TRAN_MODE = write ? SDMMC_DMA_DIR : 0;
    SDMMC->BLOCK_CFG = (blocksize << 16) | nblocks;
    SDMMC->DMA_BEG1 = (uint32_t)buf;
}

/* A read is armed before its command, because the card starts sending as soon
 * as it has answered. Then it runs to completion on its own. */
static int sd_read_data(uint32_t nblocks) {
    for (;;) {
        uint16_t fg = sd_wait_data_flag();
        int err = sd_data_flag_error(fg);
        if (err != 0) {
            return err;
        }
        if (fg & SDMMC_IF_TRANDONE) {
            return 0;
        }
        /* BKGAP after each block. On a single-block read the two can arrive
         * together or BKGAP can arrive alone, so it counts as done there. */
        if (nblocks == 1 && (fg & SDMMC_IF_BKGAP)) {
            return 0;
        }
        SDMMC->INT_FG = SDMMC_IF_BKGAP;
    }
}

/* A write is armed *after* its command, and every block after the first has to
 * be kicked off by hand.
 *
 * Both halves are load-bearing and neither is symmetric with the read path.
 * Arming a write before its command -- which is what a read needs, and what
 * symmetry suggests -- makes the command itself time out. And writing DMA_BEG1
 * once for the whole run sends the first block and stops: the reference manual
 * says a multi-block write continues only when WRITE_CONT or DMA_BEG1 is
 * written again. */
static int sd_write_data(uint32_t nblocks) {
    /* The block counter still reads the previous transfer's total until the
     * engine picks up the new arming, so wait for it to fall back to zero
     * before believing it. Without this the loop below sees a count that
     * already satisfies it, returns while the block is still on the wire, and
     * the next command lands in the middle of a transfer and times out -- with
     * the write itself having reported success. The vendor driver opens its
     * own loop with the same wait. */
    uint32_t deadline = millis() + 200;
    while (sd_blocks_done() != 0) {
        if ((int32_t)(millis() - deadline) > 0) {
            return CH32H4_SD_ETIMEDOUT;
        }
    }

    while (sd_blocks_done() < nblocks) {
        uint16_t fg = sd_wait_data_flag();
        int err = sd_data_flag_error(fg);
        if (err != 0) {
            return err;
        }
        SDMMC->INT_FG = fg;
        if (sd_blocks_done() >= nblocks) {
            break;
        }
        SDMMC->WRITE_CONT = 0;
    }
    SDMMC->BLOCK_CFG = 0;
    return 0;
}

/* CMD6, SWITCH_FUNC. The card answers with a 64-byte status block on the data
 * lines rather than in a response register, so this is a data transfer with an
 * unusual block size -- hence sd_arm() taking one.
 *
 * The 512 bits are numbered from 511 at the first bit on the wire, so bit n
 * lives in byte (511 - n) / 8 at bit position n % 8. The two fields that
 * matter here work out as:
 *
 *   bits 415:400  functions group 1 supports -> bytes 12:13, so high speed
 *                 (function 1) is status[13] bit 1
 *   bits 379:376  function group 1 actually selected -> status[16] low nibble,
 *                 which reads back 0xF when the card declined
 */
static int sd_switch_func(uint32_t arg, uint8_t *status) {
    int ret = sd_wait_dat0();
    if (ret != 0) {
        return ret;
    }
    SDMMC->INT_FG = 0xFFFF;

    uint8_t *dma = sd_dma_buf[sd_dma_half];
    sd_dma_half ^= 1;
    sd_arm(dma, SD_SWITCH_STATUS_SIZE, 1, false);
    ret = sd_cmd_r1(SD_CMD_SWITCH_FUNC, arg, SD_RESP_R1);
    if (ret != 0) {
        return ret;
    }
    ret = sd_read_data(1);
    if (ret != 0) {
        return ret;
    }
    memcpy(status, dma, SD_SWITCH_STATUS_SIZE);
    return 0;
}

/* Ask group 1 for function 1, high speed. Mode 0 in bit 31 queries without
 * changing anything and mode 1 commits; the 0xF nibbles leave the other five
 * function groups alone. Returns false for every kind of "no", including a
 * card too old to know CMD6 at all, and the caller then stays at 25 MHz. */
static bool sd_try_high_speed(void) {
    uint8_t status[SD_SWITCH_STATUS_SIZE];

    if (sd_switch_func(0x00FFFFF1, status) != 0) {
        return false;
    }
    if (!(status[13] & 0x02)) {
        return false;
    }
    if (sd_switch_func(0x80FFFFF1, status) != 0) {
        return false;
    }
    if ((status[16] & 0x0F) != 1) {
        return false;
    }
    /* The card needs eight clocks at the old rate to finish adopting the new
     * timing before the clock changes under it. */
    sd_cmd_gap();
    return true;
}

/* One contiguous run of blocks, straight into or out of `buf`, which has
 * already been checked to be somewhere the DMA can reach. */
static int sd_transfer_dma(ch32h4_sd_t *self, uint32_t block, uint8_t *buf,
                           uint32_t nblocks, bool write) {
    uint32_t addr = self->card_type == CH32H4_SD_CARD_SDHC
                        ? block : block * SD_BLOCK_SIZE;
    int ret;

    /* The card holds DAT0 low while it is still programming a previous write;
     * starting a transfer into that produces a data timeout. */
    ret = sd_wait_dat0();
    if (ret != 0) {
        return ret;
    }
    SDMMC->INT_FG = 0xFFFF;

    if (write) {
        ret = sd_cmd_r1(nblocks == 1 ? SD_CMD_WRITE_BLOCK
                                     : SD_CMD_WRITE_MULTIPLE_BLOCK,
                        addr, SD_RESP_R1);
        if (ret != 0) {
            return ret;
        }
        sd_arm(buf, SD_BLOCK_SIZE, nblocks, true);
        ret = sd_write_data(nblocks);
    } else {
        sd_arm(buf, SD_BLOCK_SIZE, nblocks, false);
        ret = sd_cmd_r1(nblocks == 1 ? SD_CMD_READ_SINGLE_BLOCK
                                     : SD_CMD_READ_MULTIPLE_BLOCK,
                        addr, SD_RESP_R1);
        if (ret != 0) {
            return ret;
        }
        ret = sd_read_data(nblocks);
    }

    if (nblocks > 1) {
        /* CMD12 ends an open-ended multi-block transfer. It is owed even when
         * the transfer failed -- more so, in fact, since the card is then
         * still streaming. */
        int stop = sd_cmd_r1(SD_CMD_STOP_TRANSMISSION, 0, SD_RESP_R1B);
        if (ret == 0) {
            ret = stop;
        }
    }
    if (ret == 0 && write) {
        ret = sd_wait_ready(self);
    }
    return ret;
}

static int sd_transfer(ch32h4_sd_t *self, uint32_t block, uint8_t *buf,
                       uint32_t nblocks, bool write) {
    if (!self->initialised) {
        return CH32H4_SD_ENODEV;
    }
    if (block + nblocks > self->block_count) {
        return CH32H4_SD_ERANGE;
    }
    if (nblocks == 0) {
        return 0;
    }

    /* Always through a bounce buffer, and alternating between two of them.
     *
     * Both halves of that are deliberate. Every buffer a sketch is likely to
     * hand over is in DTCM, which this DMA cannot reach at all, so the copy is
     * unavoidable in practice and a direct path would be dead code that is
     * never exercised. And the alternation is what makes the engine reload its
     * address: writing DMA_BEG1 with the value it already holds starts a
     * transfer without moving the pointer back to the start of the buffer, so
     * a second transfer from the same address sends whatever followed the
     * first one. Two buffers used in turn mean the register always changes. */
    for (uint32_t done = 0; done < nblocks;) {
        uint32_t n = nblocks - done;
        if (n > SD_BOUNCE_BLOCKS) {
            n = SD_BOUNCE_BLOCKS;
        }
        uint8_t *chunk = buf + done * SD_BLOCK_SIZE;
        uint8_t *dma = sd_dma_buf[sd_dma_half];
        sd_dma_half ^= 1;
        if (write) {
            memcpy(dma, chunk, n * SD_BLOCK_SIZE);
        }
        int ret = sd_transfer_dma(self, block + done, dma, n, write);
        if (ret != 0) {
            return ret;
        }
        if (!write) {
            memcpy(chunk, dma, n * SD_BLOCK_SIZE);
        }
        done += n;
    }
    return 0;
}

/* --- public --- */

void ch32h4_sd_end(void) {
    ch32h4_sd_t *self = &ch32h4_sd;
    if (!self->initialised) {
        return;
    }

    /* Let the card finish programming before the clock goes away.
     *
     * A card holds DAT0 low while it writes, and the SD specification requires
     * the clock to keep running until it lets go. Cutting SDCLK in the middle
     * of that wedges the card: it stops responding to CMD0 as well, so the
     * NEXT begin() times out in ACMD41 and every one after that does too,
     * until the board is power-cycled. Reproduced by looping bulk-write then
     * re-init -- it survived two rounds and failed on the third, which is what
     * a race on the tail of a write looks like.
     *
     * Bounded, because a card that never releases DAT0 must not hang a sketch
     * in a teardown path. Tearing down anyway is then the best available
     * option; it is already broken by that point. */
    (void)sd_wait_dat0();

    self->initialised = false;
    SDMMC->CLK_DIV = 0;
    RCC_HBPeriphResetCmd(RCC_HBPeriph_SDMMC, ENABLE);
    RCC_HBPeriphResetCmd(RCC_HBPeriph_SDMMC, DISABLE);
    RCC_HBPeriphClockCmd(RCC_HBPeriph_SDMMC, DISABLE);

    sd_pin_release(SD_PORT_CK, SD_PIN_CK);
    sd_pin_release(SD_PORT_CMD, SD_PIN_CMD);
    sd_pin_release(SD_PORT_D0, SD_PIN_D0);
    if (self->width == 4) {
        sd_pin_release(SD_PORT_D1, SD_PIN_D1);
        sd_pin_release(SD_PORT_D2, SD_PIN_D2);
        sd_pin_release(SD_PORT_D3, SD_PIN_D3);
    }
}

int ch32h4_sd_begin(uint8_t width, uint32_t freq) {
    ch32h4_sd_t *self = &ch32h4_sd;

    if (width != 1 && width != 4) {
        return CH32H4_SD_EPARAM;
    }
    if (freq < 100000) {
        return CH32H4_SD_EPARAM;
    }
    if (freq > 50000000) {
        /* 50 MHz is where SD high speed stops. Past it needs UHS-I, which
         * needs 1.8 V signalling, which needs CMD11 and a voltage switch. */
        return CH32H4_SD_EPARAM;
    }

    /* Every SDMMC pin is on VIO18. Below 2.5 V a 3.3 V card reads the clock
     * and command lines as permanently low and nothing responds -- a failure
     * that looks like bad wiring and is not. PWR_CTLR bits [12:10] select the
     * rail: 0 = 1.2 V, 1 = 1.8 V, 2 = 2.5 V, 3 = 3.3 V. */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    (void)RCC->HB1PCENR;
    if (((PWR->CTLR >> 10) & 0x7u) < 2u) {
        return CH32H4_SD_EVOLTAGE;
    }

    ch32h4_sd_end();

    self->width = width;
    self->freq = freq;
    self->block_count = 0;

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOC | RCC_HB2Periph_GPIOD
                          | RCC_HB2Periph_AFIO, ENABLE);
    (void)RCC->HB2PCENR;

    sd_pin_af(SD_PORT_CK, SD_PIN_CK);
    sd_pin_af(SD_PORT_CMD, SD_PIN_CMD);
    sd_pin_af(SD_PORT_D0, SD_PIN_D0);
    if (width == 4) {
        sd_pin_af(SD_PORT_D1, SD_PIN_D1);
        sd_pin_af(SD_PORT_D2, SD_PIN_D2);
        sd_pin_af(SD_PORT_D3, SD_PIN_D3);
    }
    sd_controller_reset(width);

    int ret = sd_card_identify(self);
    if (ret != 0) {
        /* One retry, from a full controller reset. A card that was left
         * mid-command by an earlier session can ignore the first CMD0 and
         * answer the second -- one round trip is cheap next to reporting a
         * card that is actually present as missing. */
        sd_controller_reset(width);
        ret = sd_card_identify(self);
    }
    if (ret != 0) {
        /* initialised is still false, so ch32h4_sd_end() would return without
         * doing anything. Set it so the teardown actually runs and the pins
         * and clock are released. */
        self->initialised = true;
        ch32h4_sd_end();
        return ret;
    }
    self->initialised = true;
    return 0;
}

int ch32h4_sd_read_blocks(uint32_t block, uint8_t *buf, uint32_t nblocks) {
    return sd_transfer(&ch32h4_sd, block, buf, nblocks, false);
}

int ch32h4_sd_write_blocks(uint32_t block, const uint8_t *buf,
                           uint32_t nblocks) {
    return sd_transfer(&ch32h4_sd, block, (uint8_t *)buf, nblocks, true);
}
