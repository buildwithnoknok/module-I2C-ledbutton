/*
 * noknok Keyboard Button Module Firmware  v2.2
 * CH32V003F4U6 (QFN-20)  |  Stack: cnlohr/ch32fun
 *
 * v2.2: LED driver swapped to cnlohr's ch32fun ws2812b_dma_spi_led_driver.h.
 *       The previous hand-rolled SPI/DMA driver produced a marginal waveform
 *       that genuine-but-less-tolerant SK6812 lots misread (wrong colours on
 *       ~13/20 of a batch). The library's clean waveform fixes it — same DMA,
 *       no bit-bang. See the LED section below.
 *
 * ── Bootloader-hosted build (v2.0) ────────────────────────────────────────
 *   This application runs UNDER the shared noknok I2C bootloader
 *   (module-I2C-bootloader). It is linked at the 0x1000 flash offset (app.ld)
 *   and reserves the top 16 bytes of RAM for the bootloader handoff cell at
 *   0x200007F0. Command 0xB0 drops the running module back into the bootloader
 *   so the Pico can re-flash it over I2C — no SWDIO cable. The bootloader is a
 *   separate binary, SWD-flashed once per board; this image is flashed via I2C.
 *
 * ── Hardware ──────────────────────────────────────────────────────────────
 *   PC6  SPI1_MOSI  → SK6812MINI-E LED DIN  (DMA-driven, CPU-free)
 *   PC1  I2C SDA
 *   PC2  I2C SCL
 *   PD4  Button (active-low, internal pull-up)
 *   PD1  SWIO (programming)
 *
 * ── SK6812 LED driver ─────────────────────────────────────────────────────
 *   ch32fun extralibs/ws2812b_dma_spi_led_driver.h (cnlohr) — SPI1 + DMA1-Ch3
 *   on PC6, wire order G,R,B (24 bits). Clean, glitch-free waveform via 16-bit
 *   SPI transfers, HSCR high-speed mode, very-high DMA priority and a line-low
 *   start. DMA (fire-and-forget) — never blocks the I2C / TIM2 interrupts.
 *
 * ── Enumeration ───────────────────────────────────────────────────────────
 *   Staging address : 0x7F
 *   MODULE_TYPE     : 0x03 (keyboard/button)
 *   Response (10 bytes): [UID×8][0x03][CRC8]
 *
 * ── I2C protocol after enumeration ───────────────────────────────────────
 *   Master WRITE:
 *     [0x00]              LED off
 *     [0x10, R, G, B, W]  Set LED colour (0-255 each channel)
 *     [0x11]              Reset cumulative press counter
 *     [0xB0]              Enter bootloader (reset into I2C OTA flash mode)
 *     [0xB1]              Get version — next read returns 4 version bytes
 *   Master READ (2 bytes, default):
 *     Byte 0 – status flags
 *       bit 0  current button state (1 = pressed right now)
 *       bit 1  press edge since last read  (cleared on read)
 *       bit 2  release edge since last read (cleared on read)
 *     Byte 1 – cumulative press count (wraps at 255)
 *   Master READ (4 bytes, after 0xB1):
 *     [PROTOCOL_VER, FW_MAJOR, FW_MINOR, FW_PATCH]
 *
 *   GET_VERSION (0xB1) is a noknok ecosystem-standard command (range
 *   0xB0–0xBF reserved). See Ecosystem/software/readme.md §5.
 */

#include "ch32fun.h"
#include <string.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * CONFIGURATION
 * ═══════════════════════════════════════════════════════════════════════════ */

#define ENUM_ADDR        0x7F
#define MODULE_TYPE      0x03
#define REG_ASSIGN_ADDR  0x1D

#define CMD_LED_OFF          0x00
#define CMD_LED_SET          0x10
#define CMD_CNT_RESET        0x11
#define CMD_ENTER_BOOTLOADER 0xB0   /* reset into the I2C bootloader for OTA update */
#define CMD_GET_VERSION      0xB1   /* report [PROTOCOL_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH] */

#define UID_ADDR         ((volatile uint8_t*)0x1FFFF7E8)
#define UID_LEN          8

/* ── Version reporting (noknok standard command, DEV-1) ──────────────────────
 * PROTOCOL_VERSION = which noknok protocol/API this module speaks — NOT the
 * firmware version. Bumped only when the shared protocol changes. The
 * FW_VERSION_* triple is this module's firmware semver; keep it equal to the
 * release tag. Reported on a GET_VERSION (0xB1) read. */
#define PROTOCOL_VERSION 0x01
#define FW_VERSION_MAJOR 2
#define FW_VERSION_MINOR 2
#define FW_VERSION_PATCH 0

/* Bootloader handoff cell — top 16 B of RAM, reserved by app.ld (stack ends
 * below it). Writing this magic then warm-resetting drops the module into the
 * shared noknok I2C bootloader, which sees the magic on boot and stays in flash
 * mode at 0x7E. SRAM survives a warm reset, so the magic is still there. Magic
 * and address MUST match noknok_bootloader. */
#define BL_MAGIC_CELL    (*(volatile uint32_t *)0x200007F0U)
#define BL_MAGIC_ENTER   0x6E6B4231U   /* "nkB1" */

/* ═══════════════════════════════════════════════════════════════════════════
 * STATE
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    DEV_BOOT_WAITING,
    DEV_ENUM_READY,
    DEV_ASSIGNING,
    DEV_ASSIGNED,
} DeviceState;

static volatile DeviceState dev_state = DEV_BOOT_WAITING;
static volatile uint32_t    ms_tick   = 0;
static volatile uint8_t     new_addr  = 0;
static volatile uint8_t     version_pending = 0; /* set in ISR on 0xB1; next read returns version */

/* ─── Button ─────────────────────────────────────────────────────────────── */
static volatile uint8_t btn_state    = 0;   /* 1 = currently pressed */
static volatile uint8_t btn_pressed  = 0;   /* edge flag — cleared on read */
static volatile uint8_t btn_released = 0;   /* edge flag — cleared on read */
static volatile uint8_t btn_count    = 0;   /* cumulative press count       */

/* ─── LED ────────────────────────────────────────────────────────────────── */
static volatile uint8_t  led_r = 0, led_g = 0, led_b = 0, led_w = 0;
static volatile uint8_t  led_update_pending = 0;
static volatile uint32_t flash_off_ms = 0;   /* non-zero while startup flash is active */

/* ─── I2C buffers ────────────────────────────────────────────────────────── */
#define RX_BUF_SIZE 8
#define TX_BUF_SIZE 10

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_len    = 0;
static volatile uint8_t cmd_ready = 0;

static volatile uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_len = 0;
static volatile uint8_t tx_idx = 0;


/* ═══════════════════════════════════════════════════════════════════════════
 * CRC8  (polynomial 0x07, init 0x00)
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint8_t crc8(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * BACKOFF  —  FNV-1a hash of 8-byte UID
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t backoff_ms;
static uint32_t enum_ready_start_ms = 0;

static uint32_t fnv_hash(uint32_t h)
{
    volatile uint8_t *uid = UID_ADDR;
    for (uint8_t i = 0; i < UID_LEN; i++) {
        h ^= uid[i];
        h *= 16777619UL;
    }
    return h;
}

static void calc_backoff(void)
{
    backoff_ms = (fnv_hash(2166136261UL) % 2500) + 300;
}

static uint32_t calc_rebackoff_ms(void)
{
    return ms_tick + (fnv_hash(ms_tick) % 500) + 50;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * UID RESPONSE  —  10 bytes: [UID×8][MODULE_TYPE][CRC8]
 * ═══════════════════════════════════════════════════════════════════════════ */

static void build_uid_response(void)
{
    volatile uint8_t *uid = UID_ADDR;
    for (uint8_t i = 0; i < UID_LEN; i++) tx_buf[i] = uid[i];
    tx_buf[8] = MODULE_TYPE;
    tx_buf[9] = crc8((const uint8_t*)tx_buf, 9);
    tx_len = 10;
    tx_idx = 0;
}

/* 4-byte GET_VERSION response: [PROTOCOL_VERSION, FW_MAJOR, FW_MINOR, FW_PATCH] */
static void build_version_response(void)
{
    tx_buf[0] = PROTOCOL_VERSION;
    tx_buf[1] = FW_VERSION_MAJOR;
    tx_buf[2] = FW_VERSION_MINOR;
    tx_buf[3] = FW_VERSION_PATCH;
    tx_len = 4;
    tx_idx = 0;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * SK6812MINI-E LED  —  cnlohr's proven SPI1 + DMA1-Ch3 driver (PC6 = SPI1_MOSI)
 *
 * Uses ch32fun's extralibs/ws2812b_dma_spi_led_driver.h. This replaced a
 * hand-rolled SPI/DMA driver whose marginal waveform was misread by genuine-
 * but-less-tolerant SK6812 lots (7/20 units tolerated it, 13/20 showed wrong
 * colours). The library produces a clean, glitch-free waveform — 16-bit SPI
 * transfers, HSCR high-speed mode, very-high DMA priority, and a line-low start
 * — so every unit reads it correctly. Still DMA (no bit-bang), so it never
 * blocks the I2C / TIM2 interrupts.
 *
 * Wire order is G, R, B — the driver's default order, streamed MSB-first, so the
 * colour word is packed 0x00GGRRBB. The driver owns DMA1_Channel3_IRQHandler.
 * ═══════════════════════════════════════════════════════════════════════════ */

#define WS2812DMA_IMPLEMENTATION
#define DMALEDS 4                  /* must be divisible by 4; we drive LED 0 only */
#include "ws2812b_dma_spi_led_driver.h"

static volatile uint32_t led_color = 0;   /* 0x00GGRRBB for the single LED */

uint32_t WS2812BLEDCallback(int ledno) { return (ledno == 0) ? led_color : 0; }

static void sk6812_init(void) { WS2812BDMAInit(); }

/* Set the LED colour and kick off the DMA frame. Non-blocking. */
static void sk6812_write(uint8_t r, uint8_t g, uint8_t b)
{
    led_color = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    WS2812BDMAStart(1);
}


/* ═══════════════════════════════════════════════════════════════════════════
 * BUTTON  —  PD4, active-low, internal pull-up, 20 ms debounce
 * ═══════════════════════════════════════════════════════════════════════════ */

static void btn_init(void)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOD;
    GPIOD->CFGLR   &= ~(0xF << (4 * 4));
    GPIOD->CFGLR   |=  (0x8 << (4 * 4));  /* input, pull-up/down enable */
    GPIOD->OUTDR   |=  (1 << 4);           /* select pull-up             */
}

/* Called every 1 ms from TIM2 ISR */
static void btn_update(void)
{
    static uint8_t debounce_state = 1;   /* 1 = released */
    static uint8_t debounce_cnt   = 0;

    uint8_t raw = (GPIOD->INDR >> 4) & 1;  /* 1 = released, 0 = pressed */

    if (raw == debounce_state) {
        debounce_cnt = 0;
    } else {
        if (++debounce_cnt >= 20) {          /* 20 × 1 ms = 20 ms stable */
            debounce_state = raw;
            debounce_cnt   = 0;
            if (raw == 0) {                  /* falling edge → press     */
                btn_state   = 1;
                btn_pressed = 1;
                if (btn_count < 255) btn_count++;
            } else {                         /* rising edge → release    */
                btn_state    = 0;
                btn_released = 1;
            }
        }
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * TIM2  —  1 ms tick (identical config to buzzer firmware)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void tim2_init(void)
{
    RCC->APB1PCENR  |= RCC_APB1Periph_TIM2;
    TIM2->PSC        = 47;
    TIM2->ATRLR      = 999;
    TIM2->DMAINTENR  = TIM_IT_Update;
    TIM2->SWEVGR     = TIM_UG;
    TIM2->CTLR1     |= TIM_CEN;
    NVIC_EnableIRQ(TIM2_IRQn);
}

void TIM2_IRQHandler(void) __attribute__((interrupt));
void TIM2_IRQHandler(void)
{
    TIM2->INTFR = 0;
    ms_tick++;
    btn_update();   /* accurate 1 ms debounce cadence */
}


/* ═══════════════════════════════════════════════════════════════════════════
 * I2C SLAVE  —  identical init to buzzer firmware (APB1 = 48 MHz)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void i2c_slave_init(uint8_t addr)
{
    RCC->APB2PCENR |= RCC_APB2Periph_GPIOC;
    RCC->APB1PCENR |= RCC_APB1Periph_I2C1;

    /* PC1 = SDA, PC2 = SCL: open-drain AF */
    GPIOC->CFGLR &= ~(0xF << (1 * 4)); GPIOC->CFGLR |= (0xF << (1 * 4));
    GPIOC->CFGLR &= ~(0xF << (2 * 4)); GPIOC->CFGLR |= (0xF << (2 * 4));

    I2C1->CTLR1 |=  I2C_CTLR1_SWRST;
    I2C1->CTLR1 &= ~I2C_CTLR1_SWRST;

    I2C1->CTLR2  = 48;    /* APB1 = 48 MHz */
    I2C1->CKCFGR = 240;   /* 100 kHz standard mode */
    I2C1->OADDR1 = ((uint16_t)addr << 1);

    I2C1->CTLR2 |= I2C_CTLR2_ITEVTEN | I2C_CTLR2_ITBUFEN | I2C_CTLR2_ITERREN;
    I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;

    NVIC_EnableIRQ(I2C1_EV_IRQn);
    NVIC_EnableIRQ(I2C1_ER_IRQn);
}

static void i2c_switch_addr(uint8_t addr)
{
    I2C1->CTLR1 &= ~I2C_CTLR1_PE;
    I2C1->OADDR1 = ((uint16_t)addr << 1);
    I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * I2C EVENT ISR
 * ═══════════════════════════════════════════════════════════════════════════ */

void I2C1_EV_IRQHandler(void) __attribute__((interrupt));
void I2C1_EV_IRQHandler(void)
{
    uint32_t star1 = I2C1->STAR1;
    uint32_t star2 = I2C1->STAR2;   /* reading STAR2 clears ADDR flag */

    /* ── Address matched ──────────────────────────────────────── */
    if (star1 & I2C_STAR1_ADDR) {
        rx_len = 0;
        I2C1->CTLR1 |= I2C_CTLR1_ACK;

        if (star2 & I2C_STAR2_TRA) {
            /* Master is reading — load first byte immediately */
            if (dev_state == DEV_ENUM_READY) {
                build_uid_response();
                I2C1->DATAR = tx_buf[tx_idx++];
            } else if (version_pending) {
                /* Previous write was GET_VERSION → return the 4 version bytes */
                version_pending = 0;
                build_version_response();
                I2C1->DATAR = tx_buf[tx_idx++];
            } else {
                /* Byte 0: status flags.  Byte 1: press count. */
                tx_buf[0] = (uint8_t)(btn_state
                           | (btn_pressed  << 1)
                           | (btn_released << 2));
                tx_buf[1] = btn_count;
                tx_len = 2; tx_idx = 1;
                btn_pressed  = 0;   /* edge flags cleared on read */
                btn_released = 0;
                I2C1->DATAR = tx_buf[0];
            }
        }
        return;
    }

    /* ── Byte received ────────────────────────────────────────── */
    if (star1 & I2C_STAR1_RXNE) {
        uint8_t b = (uint8_t)I2C1->DATAR;
        if (rx_len < RX_BUF_SIZE) rx_buf[rx_len++] = b;
        return;
    }

    /* ── Transmit buffer empty ────────────────────────────────── */
    if (star1 & I2C_STAR1_TXE) {
        I2C1->DATAR = (tx_idx < tx_len) ? tx_buf[tx_idx++] : 0x00;
        return;
    }

    /* ── Stop condition ───────────────────────────────────────── */
    if (star1 & I2C_STAR1_STOPF) {
        I2C1->CTLR1 |= I2C_CTLR1_PE;   /* clears STOPF */

        if (dev_state == DEV_ENUM_READY) {
            if (rx_len == 2 && rx_buf[0] == REG_ASSIGN_ADDR) {
                new_addr  = rx_buf[1];
                dev_state = DEV_ASSIGNING;
            }
        } else if (dev_state == DEV_ASSIGNED) {
            /* GET_VERSION is handled entirely in the ISR: latch it so the next
             * read returns the version bytes. Don't route it to the main loop. */
            if (rx_len == 1 && rx_buf[0] == CMD_GET_VERSION)
                version_pending = 1;
            else if (rx_len > 0)
                cmd_ready = 1;
        }

        I2C1->CTLR1 |= I2C_CTLR1_ACK;
        return;
    }
}

void I2C1_ER_IRQHandler(void) __attribute__((interrupt));
void I2C1_ER_IRQHandler(void)
{
    I2C1->STAR1 &= ~(I2C_STAR1_BERR | I2C_STAR1_ARLO |
                     I2C_STAR1_AF   | I2C_STAR1_OVR);
    I2C1->CTLR1 |= I2C_CTLR1_ACK;
}


/* ═══════════════════════════════════════════════════════════════════════════
 * COMMAND PROCESSOR
 * ═══════════════════════════════════════════════════════════════════════════ */

static void process_command(void)
{
    if (rx_len < 1) return;

    switch (rx_buf[0]) {
    case CMD_LED_OFF:
        led_r = led_g = led_b = led_w = 0;
        led_update_pending = 1;
        break;

    case CMD_LED_SET:
        if (rx_len >= 5) {
            led_r = rx_buf[1];
            led_g = rx_buf[2];
            led_b = rx_buf[3];
            led_w = rx_buf[4];
            led_update_pending = 1;
        }
        break;

    case CMD_CNT_RESET:
        btn_count = 0;
        break;

    case CMD_ENTER_BOOTLOADER:
        /* Over-the-wire firmware update requested by the Pico. Arm the handoff
         * magic in no-init RAM and warm-reset; the bootloader takes over at 0x7E.
         * NVIC_SystemReset() does not clear SRAM, so the magic survives. */
        BL_MAGIC_CELL = BL_MAGIC_ENTER;
        NVIC_SystemReset();
        break;   /* not reached */

    default:
        break;
    }
}


/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void)
{
    SystemInit();
    sk6812_init();
    btn_init();
    tim2_init();
    calc_backoff();

    __enable_irq();

    /* Startup: brief white flash — module is alive, I2C still off */
    sk6812_write(20, 20, 20);
    while (WS2812BLEDInUse);
    Delay_Ms(200);
    sk6812_write(0, 0, 0);

    while (1)
    {
        uint32_t now = ms_tick;

        /* ── Enumeration state machine ─────────────────────────── */

        if (dev_state == DEV_BOOT_WAITING && now >= backoff_ms)
        {
            enum_ready_start_ms = now;
            i2c_slave_init(ENUM_ADDR);
            dev_state = DEV_ENUM_READY;
        }

        /* Safety net: re-backoff if not assigned within 200 ms */
        if (dev_state == DEV_ENUM_READY && (now - enum_ready_start_ms) > 200)
        {
            I2C1->CTLR1 &= ~I2C_CTLR1_PE;
            backoff_ms  = calc_rebackoff_ms();
            dev_state   = DEV_BOOT_WAITING;
        }

        if (dev_state == DEV_ASSIGNING)
        {
            i2c_switch_addr(new_addr);
            dev_state = DEV_ASSIGNED;
            /* Green flash: address successfully assigned.
             * flash_off_ms schedules the turn-off 400 ms later in the main loop. */
            sk6812_write(0, 30, 0);
            flash_off_ms = ms_tick + 400;
        }

        /* ── Normal operation ──────────────────────────────────── */

        if (dev_state == DEV_ASSIGNED)
        {
            if (cmd_ready) {
                cmd_ready = 0;
                process_command();
            }

            /* Turn off startup flash after 400 ms */
            if (flash_off_ms && now >= flash_off_ms && !WS2812BLEDInUse) {
                flash_off_ms = 0;
                sk6812_write(0, 0, 0);
            }

            if (led_update_pending && !WS2812BLEDInUse && !flash_off_ms) {
                led_update_pending = 0;
                sk6812_write(led_r, led_g, led_b);
            }
        }
    }
}
