#include "Ticker.h"

extern "C" {
#include "ch32h4_timer.h"
}

/* The tick is 1 ms, so the class counts in milliseconds and the ISR does one
 * decrement per armed Ticker. A finer tick would buy resolution the API cannot
 * express -- attach_ms() takes milliseconds -- and would cost an interrupt per
 * microsecond, which on a part whose USB and Ethernet stacks also want the CPU
 * is a bad trade for nobody's benefit. */
#define TICKER_TIMER      4
#define TICKER_TICK_HZ    1000u

Ticker *Ticker::_head = nullptr;

static bool s_timer_running = false;

uint8_t Ticker::timerId() {
    return TICKER_TIMER;
}

/* Mask interrupts and report whether they were on.
 *
 * noInterrupts()/interrupts() cannot be used for this: interrupts() turns them
 * ON unconditionally, so a pair of them inside a function that was itself
 * called with interrupts masked re-enables them early -- and this code runs
 * both from a sketch and, through detach(), from inside the ISR. Reading the
 * previous state is the only version that nests. */
static inline uint32_t irq_save(void) {
    uint32_t prev;
    __asm volatile("csrrci %0, mstatus, 8" : "=r"(prev));
    return prev;
}

static inline void irq_restore(uint32_t prev) {
    if (prev & 8u) {
        __asm volatile("csrsi mstatus, 8");
    }
}

static void ticker_isr(uint8_t id, void *ctx) {
    (void)id;
    (void)ctx;
    Ticker::serviceFromIsr();
}

static bool timer_start(void) {
    if (s_timer_running) {
        return true;
    }
    if (!ch32h4_timer_claim(TICKER_TIMER, CH32H4_TIMER_TICKER)) {
        /* Held by PWM, Servo, tone or ADCInput. Refusing is the point of the
         * registry: reprogramming it here would change their period, and the
         * symptom would be a servo twitching rather than an error. */
        return false;
    }

    const int irqn = ch32h4_timer_irqn(TICKER_TIMER);
    if (irqn < 0) {
        ch32h4_timer_release(TICKER_TIMER, CH32H4_TIMER_TICKER);
        return false;
    }

    ch32h4_timer_clock_enable(TICKER_TIMER);
    ch32h4_timer_reset(TICKER_TIMER);

    TIM_TypeDef *dev = ch32h4_timer_dev(TICKER_TIMER);

    /* Timers divide HCLK on this part, never SystemCoreClock -- that is four
     * times higher on the V5F and would give a tick four times too fast. */
    const uint32_t hz = ch32h4_timer_input_clock(TICKER_TIMER);

    uint32_t top = 1000u;
    uint32_t prescaler = hz / (TICKER_TICK_HZ * top);
    if (prescaler == 0) {
        prescaler = 1;
        top = hz / TICKER_TICK_HZ;
    }

    TIM_TimeBaseInitTypeDef tb;
    TIM_TimeBaseStructInit(&tb);
    tb.TIM_Prescaler = (uint16_t)(prescaler - 1u);
    tb.TIM_Period = (uint16_t)(top - 1u);
    tb.TIM_CounterMode = TIM_CounterMode_Up;
    tb.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(dev, &tb);

    ch32h4_timer_attach_irq(TICKER_TIMER, ticker_isr, nullptr);
    TIM_ClearITPendingBit(dev, TIM_IT_Update);
    TIM_ITConfig(dev, TIM_IT_Update, ENABLE);
    NVIC_EnableIRQ((IRQn_Type)irqn);
    TIM_Cmd(dev, ENABLE);

    s_timer_running = true;
    return true;
}

/* Give TIM4 back when the last Ticker goes, so a sketch that used one early
 * does not hold a PWM timer for the rest of its life. Called with interrupts
 * already masked. */
static void timer_stop(void) {
    if (!s_timer_running) {
        return;
    }
    ch32h4_timer_detach_irq(TICKER_TIMER);
    TIM_Cmd(ch32h4_timer_dev(TICKER_TIMER), DISABLE);
    ch32h4_timer_release(TICKER_TIMER, CH32H4_TIMER_TICKER);
    s_timer_running = false;
}

/* One millisecond. Walk every armed Ticker.
 *
 * `next` is read BEFORE the callback runs, because a callback is allowed to
 * detach() itself -- a one-shot does exactly that -- and reading the link
 * afterwards would follow a pointer the callback had just cleared.
 *
 * A callback that attaches a new Ticker inserts at the head, which this walk
 * has already passed, so the new one starts on the following tick rather than
 * this one. That is the correct behaviour and not an accident of the order. */
void Ticker::serviceFromIsr() {
    Ticker *t = _head;
    while (t) {
        Ticker *next = t->_nextTicker;
        if (t->_active && t->_remaining > 0) {
            if (--t->_remaining == 0) {
                callback_t cb = t->_cb;
                callback_arg_t cbArg = t->_cbArg;
                void *arg = t->_arg;

                if (t->_repeat) {
                    t->_remaining = t->_interval;
                } else {
                    /* Detach BEFORE the callback: a one-shot that re-arms
                     * itself from inside its own callback must not then be
                     * unlinked by the detach that follows. */
                    t->detach();
                }
                if (cb) {
                    cb();
                } else if (cbArg) {
                    cbArg(arg);
                }
            }
        }
        t = next;
    }
}

Ticker::Ticker() {}

Ticker::~Ticker() {
    detach();
}

bool Ticker::arm(uint32_t ms, callback_t cb, callback_arg_t cbArg, void *arg,
                 bool repeat) {
    if (ms == 0 || (cb == nullptr && cbArg == nullptr)) {
        return false;
    }

    detach();

    if (!timer_start()) {
        return false;
    }

    const uint32_t prev = irq_save();
    _interval = ms;
    _remaining = ms;
    _cb = cb;
    _cbArg = cbArg;
    _arg = arg;
    _repeat = repeat;
    _nextTicker = _head;
    _head = this;
    _active = true;
    irq_restore(prev);
    return true;
}

bool Ticker::attach_ms(uint32_t ms, callback_t cb) {
    return arm(ms, cb, nullptr, nullptr, true);
}

bool Ticker::attach_ms(uint32_t ms, callback_arg_t cb, void *arg) {
    return arm(ms, nullptr, cb, arg, true);
}

bool Ticker::attach(float seconds, callback_t cb) {
    return attach_ms((uint32_t)(seconds * 1000.0f), cb);
}

bool Ticker::once_ms(uint32_t ms, callback_t cb) {
    return arm(ms, cb, nullptr, nullptr, false);
}

bool Ticker::once(float seconds, callback_t cb) {
    return once_ms((uint32_t)(seconds * 1000.0f), cb);
}

void Ticker::detach() {
    const uint32_t prev = irq_save();
    if (_active) {
        Ticker **link = &_head;
        while (*link && *link != this) {
            link = &(*link)->_nextTicker;
        }
        if (*link) {
            *link = _nextTicker;
        }
        _active = false;
        _nextTicker = nullptr;
    }
    if (_head == nullptr) {
        timer_stop();
    }
    irq_restore(prev);
}
