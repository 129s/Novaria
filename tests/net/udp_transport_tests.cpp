#include "net/udp_transport.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

namespace {

bool Expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        return false;
    }
    return true;
}

}  // namespace

int main() {
    bool passed = true;
    std::string error;

    novaria::net::UdpTransport receiver;
    novaria::net::UdpTransport sender;

    passed &= Expect(receiver.Open(0, error), "Receiver should open on ephemeral port.");
    passed &= Expect(error.empty(), "Receiver open should not return error.");
    passed &= Expect(receiver.IsOpen(), "Receiver should report open state.");
    passed &= Expect(receiver.LocalPort() != 0, "Receiver should expose assigned local port.");

    passed &= Expect(sender.Open(0, error), "Sender should open on ephemeral port.");
    passed &= Expect(error.empty(), "Sender open should not return error.");

    novaria::net::UdpTransport any_bind_transport;
    passed &= Expect(
        any_bind_transport.Open("0.0.0.0", 0, error),
        "Transport should open with wildcard local bind host.");
    passed &= Expect(error.empty(), "Wildcard bind open should not return error.");
    any_bind_transport.Close();

    novaria::net::UdpTransport invalid_bind_transport;
    passed &= Expect(
        !invalid_bind_transport.Open("not-an-ipv4-host", 0, error),
        "Invalid local bind host should fail transport open.");
    passed &= Expect(!error.empty(), "Invalid local bind host should return readable error.");

    const novaria::net::UdpEndpoint receiver_endpoint{
        .host = "127.0.0.1",
        .port = receiver.LocalPort(),
    };
    passed &= Expect(
        sender.SendTo(receiver_endpoint, "novaria_udp_transport_ping", error),
        "Sender should transmit datagram to receiver.");
    passed &= Expect(error.empty(), "Datagram send should not return error.");

    bool got_payload = false;
    std::string received_payload;
    novaria::net::UdpEndpoint sender_endpoint{};
    for (int index = 0; index < 200; ++index) {
        if (receiver.Receive(received_payload, sender_endpoint, error)) {
            got_payload = true;
            break;
        }
        passed &= Expect(error.empty(), "Receive polling without payload should not produce error.");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    passed &= Expect(got_payload, "Receiver should consume the transmitted datagram.");
    passed &= Expect(
        received_payload == "novaria_udp_transport_ping",
        "Received payload should match transmitted datagram.");
    passed &= Expect(
        sender_endpoint.host == "127.0.0.1",
        "Sender endpoint host should resolve as loopback.");
    passed &= Expect(sender_endpoint.port != 0, "Sender endpoint port should be non-zero.");

    passed &= Expect(
        !sender.SendTo({.host = "not-an-ipv4-host", .port = receiver.LocalPort()}, "bad", error),
        "Invalid endpoint host should fail datagram send.");
    passed &= Expect(!error.empty(), "Invalid endpoint failure should return readable error.");

    receiver.Close();
    passed &= Expect(!receiver.IsOpen(), "Receiver close should reset open state.");
    passed &= Expect(
        !receiver.Receive(received_payload, sender_endpoint, error),
        "Receive after close should fail.");
    passed &= Expect(!error.empty(), "Receive after close should return readable error.");

    sender.Close();
    passed &= Expect(!sender.IsOpen(), "Sender close should reset open state.");

    if (!passed) {
        return 1;
    }

    std::cout << "[PASS] novaria_udp_transport_tests\n";
    return 0;
}
