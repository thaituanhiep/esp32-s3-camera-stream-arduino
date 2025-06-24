// DS18B20_Sensor.h
#ifndef DS18B20_SENSOR_H
#define DS18B20_SENSOR_H

#include <OneWire.h>
#include <DallasTemperature.h>

class DS18B20_Sensor {
private:
    OneWire oneWire;
    DallasTemperature sensors;
    float lastTemp = 0;
    bool sensorConnected = false;

public:
    DS18B20_Sensor(int pin) : oneWire(pin), sensors(&oneWire) {}

    void begin() {
        sensors.begin();
        sensorConnected = sensors.getDeviceCount() > 0;
        if (!sensorConnected) {
            Serial.println("❌ Không tìm thấy DS18B20!");
        }
    }

    float getTemperature() {
        if (!sensorConnected) return NAN;

        sensors.requestTemperatures();
        lastTemp = sensors.getTempCByIndex(0);

        if (lastTemp == DEVICE_DISCONNECTED_C) {
            sensorConnected = false;
            return NAN;
        }

        return lastTemp;
    }

    bool isConnected() {
        return sensorConnected;
    }
};

#endif