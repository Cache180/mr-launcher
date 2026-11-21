/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRTransport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
using std::chrono::milliseconds;
using std::chrono::seconds;

// Reloj inyectado: una hora fija; la clase nunca lee el reloj real.
const Clock::time_point T0 = Clock::time_point {} + std::chrono::hours(1);

Clock::time_point At(seconds S) {
    return T0 + S;
}

Clock::time_point AtMs(int Ms) {
    return T0 + milliseconds(Ms);
}

std::string WithFirst(char First, std::size_t Total) {
    return std::string(1, First) + std::string(Total - 1, 'a');
}

} // namespace

TEST_CASE("MR::TransportMonitor: UDP sano durante 60 s nunca pasa a TCP") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    for (int S = 1; S <= 60; ++S) {
        if (S % 2 == 1) { // un paquete cada 2 s, el primero en t0+1s
            Monitor.OnUdpPacket(At(seconds(S)));
        }
        CHECK_FALSE(Monitor.Tick(At(seconds(S))));
        CHECK(Monitor.Current() == MR::Transport::Udp);
    }
}

TEST_CASE("MR::TransportMonitor: nunca llega UDP -> TCP a los 5 s, una sola vez") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    CHECK_FALSE(Monitor.Tick(AtMs(4900)));
    CHECK(Monitor.Current() == MR::Transport::Udp);
    CHECK(Monitor.Tick(At(seconds(5))));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick(At(seconds(6))));
    CHECK_FALSE(Monitor.Tick(At(seconds(60))));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: las constantes son las del contrato") {
    CHECK(MR::InitialUdpGrace == seconds(5));
    CHECK(MR::SessionUdpSilence == seconds(10));
}

TEST_CASE("MR::TransportMonitor: el UDP muere en sesion -> TCP tras 10 s de silencio") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    Monitor.OnUdpPacket(At(seconds(1)));
    Monitor.OnUdpPacket(At(seconds(30)));
    CHECK_FALSE(Monitor.Tick(At(seconds(30))));
    CHECK_FALSE(Monitor.Tick(AtMs(39900)));
    CHECK(Monitor.Current() == MR::Transport::Udp);
    CHECK(Monitor.Tick(At(seconds(40))));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick(At(seconds(41))));
}

TEST_CASE("MR::TransportMonitor: tras el primer paquete ya no rige la gracia de 5 s") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    Monitor.OnUdpPacket(At(seconds(4)));
    CHECK_FALSE(Monitor.Tick(At(seconds(5))));
    CHECK_FALSE(Monitor.Tick(At(seconds(9))));
    CHECK_FALSE(Monitor.Tick(AtMs(13900)));
    CHECK(Monitor.Tick(At(seconds(14))));
}

TEST_CASE("MR::TransportMonitor: sin vuelta a UDP") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    REQUIRE(Monitor.Tick(At(seconds(5))));
    Monitor.OnUdpPacket(At(seconds(6)));
    Monitor.OnUdpPacket(At(seconds(7)));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick(At(seconds(8))));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    Monitor.OnUdpStarted(At(seconds(9))); // reiniciar el canal tampoco revierte
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: forzado, el primer Tick tras OnUdpStarted da true") {
    MR::TransportMonitor Monitor(true);
    Monitor.OnUdpStarted(T0);
    CHECK(Monitor.Tick(At(seconds(1))));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick(At(seconds(2))));
}

TEST_CASE("MR::TransportMonitor: forzado, un paquete UDP no evita el TCP") {
    MR::TransportMonitor Monitor(true);
    Monitor.OnUdpStarted(T0);
    Monitor.OnUdpPacket(T0);
    CHECK(Monitor.Tick(T0));
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: Tick antes de OnUdpStarted es false") {
    MR::TransportMonitor Auto(false);
    CHECK_FALSE(Auto.Tick(At(seconds(100))));
    CHECK(Auto.Current() == MR::Transport::Udp);
    MR::TransportMonitor Forced(true);
    CHECK_FALSE(Forced.Tick(At(seconds(100))));
    CHECK(Forced.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportMonitor: dos hilos en el instante del cambio -> exactamente un true") {
    constexpr int Rounds = 300;
    for (int Round = 0; Round < Rounds; ++Round) {
        MR::TransportMonitor Monitor(false);
        Monitor.OnUdpStarted(T0);
        std::atomic<bool> Go { false };
        std::atomic<int> Trues { 0 };
        auto Worker = [&] {
            while (!Go.load()) {
            }
            if (Monitor.Tick(At(seconds(5)))) {
                ++Trues;
            }
        };
        std::thread A(Worker);
        std::thread B(Worker);
        Go = true;
        A.join();
        B.join();
        REQUIRE(Trues.load() == 1);
        REQUIRE(Monitor.Current() == MR::Transport::Tcp);
    }
}

TEST_CASE("MR::TransportMonitor: OnUdpPacket concurrente con Tick no rompe el estado") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted(T0);
    std::atomic<bool> Done { false };
    std::thread Receiver([&] {
        for (int S = 1; S <= 2000; ++S) {
            Monitor.OnUdpPacket(AtMs(S));
        }
        Done = true;
    });
    int Trues = 0;
    while (!Done.load()) {
        if (Monitor.Tick(At(seconds(1)))) {
            ++Trues;
        }
    }
    Receiver.join();
    CHECK(Trues == 0);
    CHECK(Monitor.Current() == MR::Transport::Udp);
}

// ---------------------------------------------------------------------------
// ShouldSendOverTcp con Transport::Udp reproduce la decision de ServerSend en
// src/Network/GlobalHandler.cpp (a d5e6d7c):
//     if (DLen > 3) C = Data.at(0);
//     if (C == 'O' || C == 'T') Ack = true;
//     if (C == 'N' || C == 'W' || C == 'Y' || C == 'V' || C == 'E' || C == 'C') Rel = true;
//     if (compressBound(Data.size()) > 1024) Rel = true;
//     if (Ack || Rel) { ...TCP... } else UDPSend(Data);
// Esta tabla fija esa conducta para que el cambio de ServerSend (Tarea 4) la preserve.
// ---------------------------------------------------------------------------

TEST_CASE("MR::ShouldSendOverTcp: ejemplos del contrato") {
    CHECK_FALSE(MR::ShouldSendOverTcp("p", false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp("p", false, MR::Transport::Tcp));
    CHECK_FALSE(MR::ShouldSendOverTcp("Zp:0-0:{}", false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp("Zp:0-0:{}", false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp("Cchat", false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp(std::string(5000, 'Z'), false, MR::Transport::Udp));
}

TEST_CASE("MR::ShouldSendOverTcp: codigos de primer byte (paquete de 4 bytes)") {
    // O y T: camino grande/ack. N W Y V E C: fiable. Otros: UDP.
    for (char Code : std::string("OT")) {
        CAPTURE(Code);
        CHECK(MR::ShouldSendOverTcp(WithFirst(Code, 4), false, MR::Transport::Udp));
    }
    for (char Code : std::string("NWYVEC")) {
        CAPTURE(Code);
        CHECK(MR::ShouldSendOverTcp(WithFirst(Code, 4), false, MR::Transport::Udp));
    }
    // Hoy ServerSend NO trata 't' como fiable (el contrato de la tarea lo listaba,
    // pero el codigo no): se fija el comportamiento real.
    for (char Code : std::string("tZpPUSRABDFGHIJKLMQXabcdefghijklmnopqrsuvwxyz")) {
        CAPTURE(Code);
        CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst(Code, 4), false, MR::Transport::Udp));
    }
}

TEST_CASE("MR::ShouldSendOverTcp: el primer byte solo cuenta con mas de 3 bytes") {
    // DLen > 3: con 1-3 bytes el codigo no se mira.
    for (char Code : std::string("OTNWYVEC")) {
        CAPTURE(Code);
        CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst(Code, 1), false, MR::Transport::Udp));
        CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst(Code, 2), false, MR::Transport::Udp));
        CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst(Code, 3), false, MR::Transport::Udp));
        CHECK(MR::ShouldSendOverTcp(WithFirst(Code, 4), false, MR::Transport::Udp));
    }
}

TEST_CASE("MR::ShouldSendOverTcp: Rel true siempre va por TCP") {
    CHECK(MR::ShouldSendOverTcp("p", true, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp("Zp:0-0:{}", true, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 2000), true, MR::Transport::Udp));
}

TEST_CASE("MR::ShouldSendOverTcp: umbral de tamano compressBound(n) > 1024") {
    // compressBound(n) = n + (n >> 12) + (n >> 14) + (n >> 25) + 13.
    CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst('Z', 1010), false, MR::Transport::Udp));
    CHECK_FALSE(MR::ShouldSendOverTcp(WithFirst('Z', 1011), false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 1012), false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 1013), false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 5000), false, MR::Transport::Udp));
}

TEST_CASE("MR::ShouldSendOverTcp: paquete vacio") {
    CHECK_FALSE(MR::ShouldSendOverTcp("", false, MR::Transport::Udp));
    CHECK(MR::ShouldSendOverTcp("", false, MR::Transport::Tcp));
}

TEST_CASE("MR::ShouldSendOverTcp: con Transport::Tcp todo va por TCP") {
    CHECK(MR::ShouldSendOverTcp("p", false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp("p", true, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp("Zp:0-0:{}", false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 4), false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('O', 4), false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('C', 4), false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 1011), false, MR::Transport::Tcp));
    CHECK(MR::ShouldSendOverTcp(WithFirst('Z', 5000), false, MR::Transport::Tcp));
}

TEST_CASE("MR::TransportJson") {
    CHECK(MR::TransportJson(MR::Transport::Udp) == R"({"transport":"udp"})");
    CHECK(MR::TransportJson(MR::Transport::Tcp) == R"({"transport":"tcp"})");
}
