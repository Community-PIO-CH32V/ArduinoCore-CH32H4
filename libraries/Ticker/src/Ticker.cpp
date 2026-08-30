#include "Ticker.h"

Ticker *Ticker::_head = nullptr;

Ticker::Ticker() {
    /* Linked into a list at construction, so update() can walk every Ticker
     * without anything having to register them. Tickers are usually globals,
     * so this runs before setup(). */
    _nextTicker = _head;
    _head = this;
}

Ticker::~Ticker() {
    detach();
    Ticker **p = &_head;
    while (*p) {
        if (*p == this) {
            *p = _nextTicker;
            break;
        }
        p = &(*p)->_nextTicker;
    }
}

void Ticker::arm(uint32_t ms, callback_t cb, callback_arg_t cbArg, void *arg,
                 bool repeat) {
    if (ms == 0) {
        ms = 1;   /* every millisecond, not every pass: 0 would be a busy loop */
    }
    _interval = ms;
    _cb = cb;
    _cbArg = cbArg;
    _arg = arg;
    _repeat = repeat;
    _next = millis() + ms;
    _active = true;
}

void Ticker::attach_ms(uint32_t ms, callback_t cb) {
    arm(ms, cb, nullptr, nullptr, true);
}

void Ticker::attach_ms(uint32_t ms, callback_arg_t cb, void *arg) {
    arm(ms, nullptr, cb, arg, true);
}

void Ticker::attach(float seconds, callback_t cb) {
    arm((uint32_t)(seconds * 1000.0f), cb, nullptr, nullptr, true);
}

void Ticker::once_ms(uint32_t ms, callback_t cb) {
    arm(ms, cb, nullptr, nullptr, false);
}

void Ticker::once(float seconds, callback_t cb) {
    arm((uint32_t)(seconds * 1000.0f), cb, nullptr, nullptr, false);
}

void Ticker::detach() {
    _active = false;
}

void Ticker::update() {
    const uint32_t now = millis();
    for (Ticker *t = _head; t; t = t->_nextTicker) {
        if (!t->_active) {
            continue;
        }
        /* Signed comparison, so this stays correct across the 49-day wrap of
         * millis(). `now >= t->_next` would fire every ticker at once the
         * moment the counter wrapped. */
        if ((int32_t)(now - t->_next) < 0) {
            continue;
        }

        if (t->_repeat) {
            /* Advance from the scheduled time, not from now, so a late call
             * does not make every subsequent one late as well. Catch up in
             * whole intervals if several were missed. */
            do {
                t->_next += t->_interval;
            } while ((int32_t)(now - t->_next) >= 0);
        } else {
            t->_active = false;
        }

        /* The callback last, so a callback that detaches or re-arms this
         * Ticker wins rather than being overwritten by the bookkeeping. */
        if (t->_cb) {
            t->_cb();
        } else if (t->_cbArg) {
            t->_cbArg(t->_arg);
        }
    }
}

/* The hook the core's yield() calls. Weak there, strong here, so linking this
 * library is all a sketch has to do -- Tickers then fire from delay() and from
 * the bottom of loop() without the sketch calling anything. */
extern "C" void ch32h4_ticker_update(void) {
    Ticker::update();
}
