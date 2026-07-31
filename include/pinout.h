#ifndef PINOUT_H
#define PINOUT_H

#include <Arduino.h>

constexpr uint8_t PIN_I2C_SDA = 8;
constexpr uint8_t PIN_I2C_SCL = 9;
constexpr uint32_t I2C_CLOCK_HZ = 100000;
constexpr uint16_t I2C_TIMEOUT_MS = 10;

constexpr uint8_t I2C_ADDRESS_BH1750 = 0x23;
constexpr uint8_t I2C_ADDRESS_SHT3X = 0x44;
constexpr uint8_t I2C_ADDRESS_MCP23017 = 0x20;

constexpr uint8_t RELAY_CHANNEL_COUNT = 16;

constexpr uint8_t PIN_PASSIVE_BUZZER = 16;
constexpr uint16_t BUZZER_DEFAULT_FREQUENCY_HZ = 2200;
constexpr uint8_t BUZZER_PWM_RESOLUTION_BITS = 10;

constexpr uint8_t PIN_MQ135_DO = 14;
constexpr uint8_t PIN_MQ135_AO = 4;
constexpr uint8_t MQ135_DO_ACTIVE_STATE = LOW;
constexpr float ADC_REFERENCE_VOLTAGE = 3.3f;
constexpr uint16_t ADC_MAX_READING = 4095;

constexpr uint8_t PIN_DS18B20 = 13;
constexpr uint16_t DS18B20_CONVERSION_MS = 800;

constexpr uint8_t PIN_MOTION_1 = 11;
constexpr uint8_t PIN_MOTION_2 = 10;
constexpr uint8_t MOTION_ACTIVE_STATE = HIGH;

constexpr uint8_t PIN_DOOR_REED_SWITCH = 7;
constexpr uint8_t PIN_GARAGE_REED_SWITCH = 42;
constexpr uint8_t REED_SWITCH_CLOSED_STATE = LOW;

constexpr uint8_t PIN_W25Q128_CS = 21; // ok
constexpr uint8_t PIN_W25Q128_SCK = 36;
constexpr uint8_t PIN_W25Q128_MISO = 39; // ok
constexpr uint8_t PIN_W25Q128_MOSI = 35;
constexpr uint32_t W25Q128_SPI_CLOCK_HZ = 10000000;
constexpr uint32_t W25Q128_EXPECTED_BYTES = 16UL * 1024UL * 1024UL;

constexpr uint8_t PIN_MP3_RX = 47;
constexpr uint8_t PIN_MP3_TX = 45;

constexpr uint8_t PIN_RADAR_RX = 40;
constexpr uint8_t PIN_RADAR_TX = 41;

constexpr uint8_t PIN_FM225_RX = 38;
constexpr uint8_t PIN_FM225_TX = 37;

constexpr uint8_t PIN_RDM6300_RX = 17;

constexpr uint8_t PIN_RS485_RX = 15;
constexpr uint8_t PIN_RS485_TX = 18;
constexpr uint8_t PIN_RS485_DE_RE = 48;

constexpr uint8_t PIN_CAN_TX = 5;
constexpr uint8_t PIN_CAN_RX = 6;

#endif
