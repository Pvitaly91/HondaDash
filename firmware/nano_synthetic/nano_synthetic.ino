#include <Arduino.h>
#include "endpoint.hpp"

static hd_nano::Endpoint endpoint;

void setup() {
    // Hardware UART only. Bootloader upload speed is a separate setting.
    Serial.begin(115200, SERIAL_8N1);
    endpoint.reset(millis());
}

void loop() {
    const uint32_t now = millis();
    // Never wait for an entire frame: 79 bytes exceed the core's 64-byte RX ring.
    for (uint8_t work = 0; work < 32 && Serial.available() > 0; ++work) {
        const int byte = Serial.read();
        if (byte >= 0) endpoint.receive(static_cast<uint8_t>(byte), now);
    }
    endpoint.tick(now);
    const int room = Serial.availableForWrite();
    if (room > 0) {
        uint8_t bytes[32];
        const size_t capacity = static_cast<size_t>(room < 32 ? room : 32);
        const size_t count = endpoint.read(bytes, capacity);
        // count <= availableForWrite(): HardwareSerial never waits for ring space.
        // This sketch has no other writer to Serial; ISR can only create more room.
        if (count != 0) Serial.write(bytes, count);
    }
}
