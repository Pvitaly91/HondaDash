#include "endpoint.hpp"
#include <Arduino.h>

static hd_bridge::BridgeEndpoint endpoint;

void setup() {
    // Host USB/UART only. There is no electrical DLC or GPIO backend.
    Serial.begin(115200, SERIAL_8N1);
    endpoint.reset(millis());
}
void loop() {
    const uint32_t now = millis();
    for (uint8_t work = 0; work < 32 && Serial.available() > 0; ++work) {
        const int byte = Serial.read();
        if (byte >= 0)
            endpoint.receive(static_cast<uint8_t>(byte), now);
    }
    endpoint.tick(now);
    const int room = Serial.availableForWrite();
    if (room > 0) {
        uint8_t bytes[32];
        const size_t count = endpoint.read(bytes, static_cast<size_t>(room < 32 ? room : 32));
        if (count)
            Serial.write(bytes, count);
    }
}
