#pragma once
#include "../../firmware/nano_dlc_bridge_bench/endpoint.hpp"
#include "../../firmware/nano_dlc_responder_bench/endpoint.hpp"
#include "../../firmware/shared/physical_dlc_port.hpp"
#include "application/transport.hpp"
#include "one_wire_line.hpp"
#include "protocol/protocol.hpp"
#include <array>

namespace hd_test {
// Real production embedded endpoints and driver state machines. USB is an
// in-process fragmented transport; DLC is exclusively the wired-AND bit model.
struct TwoNanoFixture {
    PairLine line;
    DriverPort bridgeDriver{line.first, line}, responderDriver{line.second, line};
    hd_bench::PhysicalDlcPort<DriverPort> bridgePort{bridgeDriver}, responderPort{responderDriver};
    hd_bench::BridgeEndpoint bridge{bridgePort};
    hd_bench::ResponderEndpoint responder{responderPort};
    hd::Time elapsed{};
    bool responderPowered{true};
    void advance(hd::Time now) {
        while (elapsed < now) {
            ++elapsed;
            line.advanceTo(static_cast<std::uint32_t>(elapsed * 2000));
            bridge.tick(line.millis());
            if (responderPowered)
                responder.tick(line.millis());
            line.service();
        }
    }
    class Usb final : public hd::Transport {
      public:
        Usb(TwoNanoFixture &fixture, bool peer) : fixture_(fixture), peer_(peer) {}
        std::vector<std::uint8_t> commands;
        void start(hd::TransportCallbacks callbacks, hd::Time now) override {
            callbacks_ = std::move(callbacks);
            open_ = true;
            callbacks_.opened(now);
        }
        void close() override { open_ = false; }
        bool send(std::span<const std::uint8_t> bytes, hd::Time now) override {
            if (!open_)
                return false;
            fixture_.advance(now);
            hd::Parser parser;
            for (const auto &frame : parser.feed(bytes))
                commands.push_back(frame.type);
            if (callbacks_.raw)
                callbacks_.raw("TX", bytes, now);
            for (auto byte : bytes) {
                if (peer_)
                    fixture_.responder.receive(byte, fixture_.line.millis());
                else
                    fixture_.bridge.receive(byte, fixture_.line.millis());
            }
            if (callbacks_.sent)
                callbacks_.sent(now);
            return true;
        }
        void tick(hd::Time now) override {
            fixture_.advance(now);
            if (!open_)
                return;
            for (unsigned chunk = 0; chunk != 24; ++chunk) {
                std::array<std::uint8_t, 7> bytes{};
                const auto count = peer_ ? fixture_.responder.read(bytes.data(), bytes.size())
                                         : fixture_.bridge.read(bytes.data(), bytes.size());
                if (!count)
                    break;
                const auto span = std::span(bytes).first(count);
                if (callbacks_.raw)
                    callbacks_.raw("RX", span, now);
                if (callbacks_.received)
                    callbacks_.received(span, now);
                if (!open_)
                    break;
            }
        }
        bool isOpen() const override { return open_; }
        std::size_t pendingBytes() const override {
            return peer_ ? fixture_.responder.available() : fixture_.bridge.available();
        }
        std::string name() const override {
            return peer_ ? "software USB responder; production bit-line model"
                         : "software USB bridge; production bit-line model";
        }

      private:
        TwoNanoFixture &fixture_;
        bool peer_, open_{};
        hd::TransportCallbacks callbacks_;
    };
};
} // namespace hd_test
