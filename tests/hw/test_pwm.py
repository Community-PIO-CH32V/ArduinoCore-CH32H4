"""analogWrite over the variant's pin map and the core's timer registry."""


def _kv(out):
    d = {}
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


def test_many_pins_can_do_pwm(board):
    """Not four. The package gives timer channels to most of its pads, and the
    map is what makes them reachable."""
    d = _kv(board.command("pwmmap"))
    assert int(d["pwm_pins"]) >= 60, d
    assert int(d["pwm_timers"]) >= 8, d


def test_duty_is_what_was_asked_for(board):
    d = _kv(board.command("pwmtest"))
    assert int(d["pwm_duty_pct"]) == 50, d


def test_the_timer_is_claimed_and_released(board):
    d = _kv(board.command("pwmtest"))
    assert d["pwm_owner"] == "PWM", d
    assert d["pwm_released"] == "1", d


def test_two_pins_share_one_timer(board):
    """A timer's four channels share its period register, so pins on one timer
    must join it rather than each taking a fresh timer -- twelve go quickly."""
    d = _kv(board.command("pwmshare"))
    assert d["share_same_timer"] == "1", d


def test_releasing_one_channel_keeps_the_timer_for_the_other(board):
    d = _kv(board.command("pwmshare"))
    assert d["still_owned"] == "1", d
    assert d["now_free"] == "1", d


def test_a_claimed_timer_is_refused_to_pwm(board):
    d = _kv(board.command("timerclaim"))
    assert d["servo_claimed_tim2"] == "1", d
    assert d["pwm_refused"] == "1", d
    assert d["holder"] == "Servo", d


def test_analogwrite_falls_back_to_another_timer(board):
    """PA0 is TIM2 CH1, TIM5 CH1 and TIM9 CH1. With TIM2 taken by Servo,
    analogWrite must use one of the others rather than producing a broken
    output on the timer it wanted."""
    d = _kv(board.command("timerclaim"))
    fell_back = int(d["fellback_to_timer"])
    assert fell_back != 0, "analogWrite gave up instead of using another timer"
    assert fell_back != 2, "analogWrite used the timer Servo holds"
