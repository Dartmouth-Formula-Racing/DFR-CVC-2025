/*
 * torque.c
 *
 * Created on September 20, 2024
 * Andrei Gerashchenko
 */

#include <cvc/can.h>
#include <cvc/data.h>
#include <cvc/parse.h>
#include <cvc/statemachine.h>
#include <cvc/torque.h>
#include <main.h>
#include <stdbool.h>

bool Inverter1_Clear_Flag;
bool Inverter2_Clear_Flag;

void Torque_CalculateAcceleration() {
    static uint32_t last_time_1 = 0;
    static uint32_t last_time_2 = 0;

    static int32_t last_speed_1 = 0;
    static int32_t last_speed_2 = 0;

    static float accelerations_1[INVERTER_ACCEL_AVERAGE] = {0};
    static float accelerations_2[INVERTER_ACCEL_AVERAGE] = {0};

    static uint8_t accel_index_1 = 0;
    static uint8_t accel_index_2 = 0;
    if (Inverter1_Position_Flag) {
        CAN_Parse_Inverter_HighSpeedParameters(0);
        uint32_t now = HAL_GetTick();
        int16_t speed = (int16_t)CVC_data[INVERTER1_MOTOR_SPEED_HS];
        // uint32_t time_diff = now - last_time_1; // ms
        uint32_t time_diff = 3;                     // ms, hardcoded for now
        int16_t speed_diff = speed - last_speed_1;  // RPM

        if (time_diff == 0) {
            return;
        }
        // Calculate acceleration in RPM/s
        float acceleration = ((float)speed_diff / (float)time_diff);
        accelerations_1[accel_index_1] = acceleration;
        accel_index_1 = (accel_index_1 + 1) % INVERTER_ACCEL_AVERAGE;

        float average_acceleration = 0;
        for (uint8_t i = 0; i < INVERTER_ACCEL_AVERAGE; i++) {
            average_acceleration += accelerations_1[i];
        }
        average_acceleration /= INVERTER_ACCEL_AVERAGE;
        CVC_data[INVERTER1_MOTOR_ACCELERATION] = (uint64_t)((int64_t)(average_acceleration * ACCEL_INT_FLOAT_SCALING));

        last_time_1 = now;
        last_speed_1 = speed;
        Inverter1_Position_Flag = 0;
    }

    if (Inverter2_Position_Flag) {
        CAN_Parse_Inverter_HighSpeedParameters(1);
        uint32_t now = HAL_GetTick();
        int16_t speed = (int16_t)CVC_data[INVERTER2_MOTOR_SPEED_HS];
        // uint32_t time_diff = now - last_time_2; // ms
        uint32_t time_diff = 3;                     // ms, hardcoded for now
        int16_t speed_diff = speed - last_speed_2;  // RPM

        if (time_diff == 0) {
            return;
        }
        // Calculate acceleration in RPM/s
        float acceleration = ((float)speed_diff / (float)time_diff);
        accelerations_2[accel_index_2] = acceleration;
        accel_index_2 = (accel_index_2 + 1) % INVERTER_ACCEL_AVERAGE;

        float average_acceleration = 0;
        for (uint8_t i = 0; i < INVERTER_ACCEL_AVERAGE; i++) {
            average_acceleration += accelerations_2[i];
        }
        average_acceleration /= INVERTER_ACCEL_AVERAGE;
        CVC_data[INVERTER2_MOTOR_ACCELERATION] = (uint64_t)((int64_t)(average_acceleration * ACCEL_INT_FLOAT_SCALING));

        last_time_2 = now;
        last_speed_2 = speed;
        Inverter2_Position_Flag = 0;
    }
}

void Torque_CalculateAvailableTorque() {
#if INVERTER_USE_HS_DATA
    CAN_Parse_Inverter_HighSpeedParameters(0);
    CAN_Parse_Inverter_HighSpeedParameters(1);

    volatile float Inverter1_voltage = (float)((int16_t)CVC_data[INVERTER1_DC_BUS_VOLTAGE_HS]) * 0.1;  // High speed message
    volatile float Inverter2_voltage = (float)((int16_t)CVC_data[INVERTER2_DC_BUS_VOLTAGE_HS]) * 0.1;  // High speed message
    volatile int16_t Inverter1_rpm = (int16_t)CVC_data[INVERTER1_MOTOR_SPEED_HS];                      // High speed message
    volatile int16_t Inverter2_rpm = (int16_t)CVC_data[INVERTER2_MOTOR_SPEED_HS];                      // High speed message
#else
    CAN_Parse_Inverter_VoltageParameters(0);
    CAN_Parse_Inverter_VoltageParameters(1);
    CAN_Parse_Inverter_MotorPositionParameters(0);
    CAN_Parse_Inverter_MotorPositionParameters(1);

    volatile float Inverter1_voltage = (float)((int16_t)CVC_data[INVERTER1_DC_BUS_VOLTAGE]) * 0.1;  // Fast message
    volatile float Inverter2_voltage = (float)((int16_t)CVC_data[INVERTER2_DC_BUS_VOLTAGE]) * 0.1;  // Fast message
    volatile int16_t Inverter1_rpm = (int16_t)CVC_data[INVERTER1_MOTOR_SPEED];                      // Fast message
    volatile int16_t Inverter2_rpm = (int16_t)CVC_data[INVERTER2_MOTOR_SPEED];                      // Fast message
#endif

    // Use absolute values for motor speeds
    if (Inverter1_rpm < 0) {
        Inverter1_rpm *= -1;
    }
    if (Inverter2_rpm < 0) {
        Inverter2_rpm *= -1;
    }
    // Add 1 rpm to avoid division by zero
    Inverter1_rpm++;
    Inverter2_rpm++;

    // Allowable bus voltage sag before hitting an undervoltage limit
    volatile float Inverter1_max_sag = Inverter1_voltage - BUS_MIN_VOLTAGE;
    volatile float Inverter2_max_sag = Inverter2_voltage - BUS_MIN_VOLTAGE;
    if (Inverter1_max_sag < 0) {
        Inverter1_max_sag = 0;
    }
    if (Inverter2_max_sag < 0) {
        Inverter2_max_sag = 0;
    }

    // Current at which the bus voltage will sag to the minimum voltage
    volatile float Inverter1_max_current = Inverter1_voltage / BUS_RESISTANCE;
    volatile float Inverter2_max_current = Inverter2_voltage / BUS_RESISTANCE;
    if (Inverter1_max_current > INVERTER_CURRENT_LIMIT) {
        Inverter1_max_current = INVERTER_CURRENT_LIMIT;
    }
    if (Inverter2_max_current > INVERTER_CURRENT_LIMIT) {
        Inverter2_max_current = INVERTER_CURRENT_LIMIT;
    }

    // P = IV = τω
    // V = bus voltage, I = max inverter current, ω = motor speed
    // Calculate torque at which the bus voltage will sag to the minimum voltage
    // volatile float max_inverter1_torque = Inverter1_voltage * Inverter1_max_current / (Inverter1_rpm * RPM_TO_RADS);
    // volatile float max_inverter2_torque = Inverter2_voltage * Inverter2_max_current / (Inverter2_rpm * RPM_TO_RADS);

    // τ = Kτ * I
    volatile float max_inverter1_torque = TORQUE_CONSTANT * Inverter1_max_current;
    volatile float max_inverter2_torque = TORQUE_CONSTANT * Inverter2_max_current;

    if (max_inverter1_torque > ((NOMINAL_TORQUE * TORQUE_LIMIT) / 100.0) || max_inverter1_torque < 0) {
        CVC_data[CVC_INVERTER1_TORQUE_LIMIT] = (int16_t)((NOMINAL_TORQUE * TORQUE_LIMIT) / 100.0);
    } else {
        CVC_data[CVC_INVERTER1_TORQUE_LIMIT] = (int16_t)(max_inverter1_torque + 0.5);
    }
    if (max_inverter2_torque > ((NOMINAL_TORQUE * TORQUE_LIMIT) / 100.0) || max_inverter2_torque < 0) {
        CVC_data[CVC_INVERTER2_TORQUE_LIMIT] = (int16_t)((NOMINAL_TORQUE * TORQUE_LIMIT) / 100.0);
    } else {
        CVC_data[CVC_INVERTER2_TORQUE_LIMIT] = (int16_t)(max_inverter2_torque + 0.5);
    }
}

void Torque_CalculateTorque() {
    static uint32_t last = 0;
    if (HAL_GetTick() - last < TORQUE_PERIOD) {
        return;
    }
    last = HAL_GetTick();

    CAN_Parse_Inverter_AnalogInputStatus(1);

    volatile float throttle = (float)((uint16_t)CVC_data[CVC_THROTTLE]) / 1000;
    volatile bool throttle_valid = CVC_data[CVC_THROTTLE_VALID];
    volatile float left_accel = (float)((int64_t)CVC_data[INVERTER1_MOTOR_ACCELERATION]) / ACCEL_INT_FLOAT_SCALING;
    volatile float right_accel = (float)((int64_t)CVC_data[INVERTER2_MOTOR_ACCELERATION]) / ACCEL_INT_FLOAT_SCALING;

    if (CVC_data[CVC_DRIVE_MODE] == DRIVE) {
        left_accel *= -1;
    } else if (CVC_data[CVC_DRIVE_MODE] == REVERSE) {
        right_accel *= -1;
    }

    volatile float steering_voltage = (float)((int32_t)CVC_data[INVERTER1_ANALOG_INPUT_1]) * 0.01;
    volatile float steering_angle = 2 * (steering_voltage - STEERING_POT_LEFT) / (STEERING_POT_RIGHT - STEERING_POT_LEFT) - 1;
    volatile int16_t left_torque = 0;
    volatile int16_t right_torque = 0;
    volatile uint8_t left_direction = 0;
    volatile uint8_t right_direction = 1;
    volatile int16_t max_left_torque = (int16_t)(CVC_data[CVC_INVERTER1_TORQUE_LIMIT]);
    volatile int16_t max_right_torque = (int16_t)(CVC_data[CVC_INVERTER2_TORQUE_LIMIT]);

    // Clamp steering angle to -1 to 1
    if (steering_angle > 1) {
        steering_angle = 1;
    } else if (steering_angle < -1) {
        steering_angle = -1;
    }

    if (!throttle_valid || CVC_data[CVC_STATE] != READY_TO_DRIVE) {
        left_torque = 0;
        right_torque = 0;
    } else {
        if (CVC_data[CVC_DRIVE_MODE] == DRIVE) {
            // Calculate torque for drive mode
            left_torque = (int16_t)(max_left_torque * 10 * throttle);
            right_torque = (int16_t)(max_right_torque * 10 * throttle);

            // Torque vectoring
            if (steering_angle < 0) {  // Left turn
                left_torque += (int16_t)(TORQUE_VECTORING_GAIN * left_torque * steering_angle);
            } else {  // Right turn
                right_torque -= (int16_t)(TORQUE_VECTORING_GAIN * right_torque * steering_angle);
            }

            // Traction control
            // if (left_accel > TRACTION_CONTROL_MAX_ACCEL) {
            //     left_torque -= (int16_t)(left_torque * TRACTION_CONTROL_GAIN * (left_accel - TRACTION_CONTROL_MAX_ACCEL));
            // }
            // if (right_accel > TRACTION_CONTROL_MAX_ACCEL) {
            //     right_torque -= (int16_t)(right_torque * TRACTION_CONTROL_GAIN * (right_accel - TRACTION_CONTROL_MAX_ACCEL));
            // }

            left_direction = 0;
            right_direction = 1;
        } else if (CVC_data[CVC_DRIVE_MODE] == REVERSE) {
            // Calculate torque for drive mode
            left_torque = (int16_t)(max_left_torque * 10 * (throttle * REVERSE_TORQUE_LIMIT / 100.0));
            right_torque = (int16_t)(max_right_torque * 10 * (throttle * REVERSE_TORQUE_LIMIT / 100.0));

            // Torque vectoring
            if (steering_angle < 0) {  // Left turn
                left_torque += (int16_t)(TORQUE_VECTORING_GAIN * left_torque * steering_angle);
            } else {  // Right turn
                right_torque -= (int16_t)(TORQUE_VECTORING_GAIN * right_torque * steering_angle);
            }

            left_direction = 1;
            right_direction = 0;
        }
    }

    // Apply command scaling factor (121 Nm at inverter = 100 Nm at motor)
    left_torque = (int16_t)(left_torque * TORQUE_COMMAND_SCALE);
    right_torque = (int16_t)(right_torque * TORQUE_COMMAND_SCALE);

    // Undervoltage & overcurrent protection
    if (left_torque > max_left_torque * 10 * TORQUE_COMMAND_SCALE) {
        left_torque = max_left_torque * 10 * TORQUE_COMMAND_SCALE;
    }
    if (right_torque > max_right_torque * 10 * TORQUE_COMMAND_SCALE) {
        right_torque = max_right_torque * 10 * TORQUE_COMMAND_SCALE;
    }

    // Final sanity check
    if (left_torque > NOMINAL_TORQUE * 10) {
        left_torque = NOMINAL_TORQUE * 10;
    } else if (left_torque < 0) {  // Assumes no regen for now, regen requires opposing torque value
        left_torque = 0;
    }
    if (right_torque > NOMINAL_TORQUE * 10) {
        right_torque = NOMINAL_TORQUE * 10;
    } else if (right_torque < 0) {  // Assumes no regen for now, regen requires opposing torque value
        right_torque = 0;
    }

    CVC_data[CVC_LEFT_TORQUE] = left_torque;
    CVC_data[CVC_RIGHT_TORQUE] = right_torque;
    CVC_data[CVC_LEFT_DIRECTION] = left_direction;
    CVC_data[CVC_RIGHT_DIRECTION] = right_direction;
}

void Torque_ClearFaults() {
    static uint32_t last = 0;
    if (HAL_GetTick() - last < TORQUE_PERIOD) {
        return;
    }
    last = HAL_GetTick();
    if (Inverter1_Clear_Flag) {
        CAN_Queue_Frame_t left_command;
        for (uint8_t i = 0; i < 8; i++) {
            left_command.data[i] = 0;
        }
        left_command.Tx_header.DLC = 8;
        left_command.Tx_header.IDE = (CAN_INVERTER_USE_EXT) ? CAN_ID_EXT : CAN_ID_STD;
        left_command.Tx_header.RTR = CAN_RTR_DATA;
        if (CAN_INVERTER_USE_EXT) {
            left_command.Tx_header.ExtId = CAN_INVERTER_BASE_ID1_29 + 33;
        } else {
            left_command.Tx_header.StdId = CAN_INVERTER_BASE_ID1_11 + 33;
        }

        // Parameter 20 - Fault Clear
        left_command.data[0] = 20;
        left_command.data[1] = 0;
        // Write
        left_command.data[2] = 1;
        CAN_Queue_TX(&left_command);
        Inverter1_Clear_Flag = false;
    }
    if (Inverter2_Clear_Flag) {
        CAN_Queue_Frame_t right_command;
        for (uint8_t i = 0; i < 8; i++) {
            right_command.data[i] = 0;
        }
        right_command.Tx_header.DLC = 8;
        right_command.Tx_header.IDE = (CAN_INVERTER_USE_EXT) ? CAN_ID_EXT : CAN_ID_STD;
        right_command.Tx_header.RTR = CAN_RTR_DATA;
        if (CAN_INVERTER_USE_EXT) {
            right_command.Tx_header.ExtId = CAN_INVERTER_BASE_ID2_29 + 33;
        } else {
            right_command.Tx_header.StdId = CAN_INVERTER_BASE_ID2_11 + 33;
        }

        // Parameter 20 - Fault Clear
        right_command.data[0] = 20;
        right_command.data[1] = 0;
        // Write
        right_command.data[2] = 1;
        CAN_Queue_TX(&right_command);
        Inverter2_Clear_Flag = false;
    }
    
}

void Torque_SendTorque() {
    static bool first_enable = false;
    static uint32_t last = 0;
    if (HAL_GetTick() - last < TORQUE_PERIOD) {
        return;
    }
    last = HAL_GetTick();

    CAN_Queue_Frame_t left_command;
    CAN_Queue_Frame_t right_command;

    for (uint8_t i = 0; i < 8; i++) {
        left_command.data[i] = 0;
        right_command.data[i] = 0;
    }

    // Bytes 2-3: Speed command (-32768 to 32767) RPM [2, 3]
    left_command.data[2] = 0;
    right_command.data[2] = 0;
    left_command.data[3] = 0;
    right_command.data[3] = 0;

    // Byte 4: Direction (0 = reverse, 1 = forward) [4]
    left_command.data[4] = CVC_data[CVC_LEFT_DIRECTION];
    right_command.data[4] = CVC_data[CVC_RIGHT_DIRECTION];

    // Bytes 6-7: Torque limit (-32768 to 32767) Nm/10 [6, 7]
    left_command.data[6] = 0;
    right_command.data[6] = 0;
    left_command.data[7] = 0;
    right_command.data[7] = 0;

    if (CVC_data[CVC_STATE] == READY_TO_DRIVE) {
        if (CVC_data[CVC_DRIVE_MODE] == DRIVE || CVC_data[CVC_DRIVE_MODE] == REVERSE) {
            // Bytes 0-1: Torque command (-32768 to 32767) Nm/10 [0, 1]
            left_command.data[0] = CVC_data[CVC_LEFT_TORQUE] & 0xFF;
            right_command.data[0] = CVC_data[CVC_RIGHT_TORQUE] & 0xFF;
            left_command.data[1] = (CVC_data[CVC_LEFT_TORQUE] >> 8) & 0xFF;
            right_command.data[1] = (CVC_data[CVC_RIGHT_TORQUE] >> 8) & 0xFF;

            // Byte 5 bit 0: Inverter enable (0 = disable, 1 = enable) [5]
            // Byte 5 bit 1: Inverter discharge (0 = disable, 1 = enable) [5]
            // Byte 5 bit 2: Speed mode enable (0 = disable, 1 = enable) [5]
            if (DISABLE_ON_ZERO_THROTTLE) {
                if ((float)((uint16_t)CVC_data[CVC_THROTTLE]) * 0.01 > 0.0) {
                    left_command.data[5] = 0x01;   // Inverter enabled, discharge disabled, speed mode disabled
                    right_command.data[5] = 0x01;  // Inverter enabled, discharge disabled, speed mode disabled
                } else {
                    left_command.data[5] = 0x00;   // Inverter disabled, discharge disabled, speed mode disabled
                    right_command.data[5] = 0x00;  // Inverter disabled, discharge disabled, speed mode disabled
                }
            } else if (!first_enable) {
                left_command.data[5] = 0x01;   // Inverter enabled, discharge disabled, speed mode disabled
                right_command.data[5] = 0x01;  // Inverter enabled, discharge disabled, speed mode disabled
            }
        }
        if (first_enable) {
            first_enable = false;
        }
    } else {
        first_enable = false;
    }

    left_command.Tx_header.DLC = 8;
    left_command.Tx_header.IDE = (CAN_INVERTER_USE_EXT) ? CAN_ID_EXT : CAN_ID_STD;
    left_command.Tx_header.RTR = CAN_RTR_DATA;
    if (CAN_INVERTER_USE_EXT) {
        left_command.Tx_header.ExtId = CAN_INVERTER_BASE_ID1_29 + 32;
    } else {
        left_command.Tx_header.StdId = CAN_INVERTER_BASE_ID1_11 + 32;
    }

    right_command.Tx_header.DLC = 8;
    right_command.Tx_header.IDE = (CAN_INVERTER_USE_EXT) ? CAN_ID_EXT : CAN_ID_STD;
    right_command.Tx_header.RTR = CAN_RTR_DATA;
    if (CAN_INVERTER_USE_EXT) {
        right_command.Tx_header.ExtId = CAN_INVERTER_BASE_ID2_29 + 32;
    } else {
        right_command.Tx_header.StdId = CAN_INVERTER_BASE_ID2_11 + 32;
    }

    if (LEFT_MOTOR_ENABLE) {
        CAN_Queue_TX(&left_command);
    }
    if (RIGHT_MOTOR_ENABLE) {
        CAN_Queue_TX(&right_command);
    }
}