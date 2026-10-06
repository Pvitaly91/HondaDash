#include "endpoint.hpp"
#include "../shared/physical_dlc_port.hpp"
#include "../shared/one_wire_avr.hpp"
#include <Arduino.h>
static hd_onewire::AvrPort line;
static hd_bench::PhysicalDlcPort<hd_onewire::AvrPort> port(line);
static hd_bench::BridgeEndpoint endpoint(port);
void setup() {
    line.begin(); // Released open-collector output; USB handshake emits no line bytes.
    Serial.begin(115200, SERIAL_8N1);
    endpoint.reset(millis());
}
void loop() {
    line.service();
    const uint32_t now = millis();
    endpoint.tick(now); // Drain physical RX before dispatching a proposed boundary.
    for (uint8_t n = 0; n < 32 && Serial.available() > 0; ++n) {
        const int value = Serial.read();
        if (value >= 0) endpoint.receive(uint8_t(value), now);
    }
    const int room = Serial.availableForWrite();
    if (room > 0) {
        uint8_t bytes[32];
        const size_t n = endpoint.read(bytes, size_t(room < 32 ? room : 32));
        if (n) Serial.write(bytes, n);
    }
}
