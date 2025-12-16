# IoT-Based Air Quality Monitoring System

An IoT air quality monitoring system using MQ-series gas sensors and temperature/humidity sensors, with ESP8266 sensor nodes communicating wirelessly to a central ESP32 for aggregation and monitoring.

## Project Overview

This project demonstrates a scalable, low-cost air quality monitoring architecture where multiple sensing nodes collect environmental data and transmit it to a central controller for logging, alerts, and future cloud integration.

## System Architecture

* **Sensor Node**: ESP8266-12 + MQ135 (simulated with MQ2) + DHT11/DHT22
* **Central Node**: ESP32 (data aggregation and control)
* **Communication**: Wi‑Fi (MQTT / ESP‑NOW planned)

## Hardware Components

* ESP8266‑12 (sensor node)
* ESP32 (central unit)
* MQ135 Gas Sensor (MQ2 used in simulation)
* DHT11 / DHT22 Temperature & Humidity Sensor
* Buzzer (optional alarm)
* Power supply and basic passives

## Simulation Environment

* **Wokwi**: Primary simulator (ESP8266/ESP32, Wi‑Fi, DHT, MQ sensors)
* **Tinkercad**: Limited use for basic analog sensor behavior only

> Note: Gas concentration accuracy and real calibration are not possible in simulators; only logic and communication are validated.

## Features

* Real‑time gas level monitoring (analog)
* Temperature and humidity sensing
* Serial monitoring for debugging
* Threshold‑based alert logic (buzzer via MCU)
* Modular design for multi‑node expansion

## Calibration (Real Hardware)

1. Burn‑in MQ135 sensor for 24–48 hours
2. Measure baseline resistance (R₀) in clean air
3. Apply temperature/humidity compensation using DHT data
4. Use gas curves only after baseline calibration

## Project Status

* ✔️ Sensor interfacing (simulation)
* ✔️ ESP8266/ESP32 firmware testing
* ⏳ Wireless protocol integration
* ⏳ Real‑world calibration & deployment

## Future Improvements

* MQTT cloud dashboard
* Data logging (SD / cloud)
* OTA firmware updates
* Battery‑powered sensor nodes

## License

This project is for educational and research purposes.
