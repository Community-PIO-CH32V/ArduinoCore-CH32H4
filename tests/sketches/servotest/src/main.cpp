#include <Arduino.h>
#include <Servo.h>

Servo s1, s2;

void setup() {
  Serial1.begin(115200);
  Serial1.println("servotest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "servoattach") {
          uint8_t ch = s1.attach(PA0);
          Serial1.print("servo_channel="); Serial1.println(ch);
          Serial1.print("servo_timer="); Serial1.println(s1.timer());
          Serial1.print("servo_owner=");
          Serial1.println(ch32h4_timer_owner_name(ch32h4_timer_owner(s1.timer())));
          s1.write(90);
          Serial1.print("servo_us="); Serial1.println(s1.readMicroseconds());
          Serial1.print("servo_angle="); Serial1.println(s1.read());
        } else if (line == "servovspwm") {
          /* A servo must not join analogWrite's timer: they need different
             frame rates and the period register is shared. */
          s1.detach(); s2.detach();
          analogWriteStop(PA0);
          analogWrite(PA1, 128);            /* PWM takes a timer */
          ch32h4_pwm_af_t af;
          ch32h4_pwm_find_active(PA1, &af);
          uint8_t pwm_timer = af.timer;
          uint8_t ch = s1.attach(PA0);      /* PA0 shares TIM2 with PA1 */
          Serial1.print("pwm_timer="); Serial1.println(pwm_timer);
          Serial1.print("servo_timer="); Serial1.println(s1.timer());
          Serial1.print("servo_attached="); Serial1.println(ch != INVALID_SERVO ? 1 : 0);
          Serial1.print("different_timers=");
          Serial1.println((s1.timer() != 0 && s1.timer() != pwm_timer) ? 1 : 0);
          s1.detach(); analogWriteStop(PA1);
        } else if (line == "servodetach") {
          s1.attach(PA0);
          uint8_t t = s1.timer();
          s1.detach();
          Serial1.print("released=");
          Serial1.println(ch32h4_timer_owner(t) == CH32H4_TIMER_FREE ? 1 : 0);
        }
        Serial1.print("> ");
        line = "";
      }
    } else {
      line += c;
    }
  }
  yield();
}
