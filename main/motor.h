#ifndef MOTOR_H
#define MOTOR_H

typedef enum {
    MOTOR_CW = 1,
    MOTOR_CCW = 0
} motor_direction_t;

void motor_init(void);
void motor_off(void);
void step_once(int clockwise);

/* Runs the motor for exactly 5 seconds in the chosen direction */
void motor_run_1(motor_direction_t direction);
void motor_run_2(motor_direction_t direction);
void motor_run_3(motor_direction_t direction);
void motor_run_4(motor_direction_t direction);
void motor_run_5(motor_direction_t direction);
void motor_run_6(motor_direction_t direction);

/* Optional test task version */
void stepper_test(void *pvParameters);

#endif // MOTOR_H