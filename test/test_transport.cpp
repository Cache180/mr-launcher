/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRTransport.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <string>
#include <thread>

namespace {

using std::chrono::milliseconds;
using std::chrono::seconds;

// Ft de mentira para las sesiones que no la necesitan.
const std::function<void()> NoFt = [] {};

std::string WithFirst(char First, std::size_t Total) {
    return std::string(1, First) + std::string(Total - 1, 'a');
}

// N esperas de 1 s agotadas sin datagrama (lo que hace UDPRcv con SO_RCVTIMEO de 1 s).
void WaitSeconds(MR::TransportMonitor& Monitor, int N) {
    for (int I = 0; I < N; ++I) {
        Monitor.OnUdpWait(seconds(1));
    }
}

void WaitSeconds(MR::TransportSession& Session, std::uint64_t Generation, int N) {
    for (int I = 0; I < N; ++I) {
        Session.OnUdpWait(Generation, seconds(1));
    }
}

} // namespace

// El silencio UDP solo cuenta mientras el receptor espera en el socket (OnUdpWait); el
// tiempo fuera de la recepcion (reenviar al juego, juego congelado) nunca se mide.

TEST_CASE("MR::TransportMonitor: UDP sano durante 60 s nunca pasa a TCP") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    for (int S = 1; S <= 60; ++S) {
        if (S % 2 == 1) { // un datagrama y una espera agotada alternos
            Monitor.OnUdpPacket();
        } else {
            Monitor.OnUdpWait(seconds(1));
        }
        CHECK_FALSE(Monitor.Tick());
        CHECK(Monitor.Current() == MR::Transport::Udp);
    }
}

TEST_CASE("MR::TransportMonitor: nunca llega UDP -> TCP con 5 s de espera, una sola vez") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    WaitSeconds(Monitor, 4);
    Monitor.OnUdpWait(milliseconds(900));
    CHECK_FALSE(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Udp);
    Monitor.OnUdpWait(milliseconds(100));
    CHECK(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick());
    WaitSeconds(Monitor, 60);
    CHECK_FALSE(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: las constantes son las del contrato") {
    CHECK(MR::InitialUdpGrace == seconds(5));
    CHECK(MR::SessionUdpSilence == seconds(10));
}

TEST_CASE("MR::TransportMonitor: el UDP muere en sesion -> TCP con 10 s de espera") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    Monitor.OnUdpPacket();
    WaitSeconds(Monitor, 9);
    Monitor.OnUdpWait(milliseconds(900));
    CHECK_FALSE(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Udp);
    Monitor.OnUdpWait(milliseconds(100));
    CHECK(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: tras el primer paquete ya no rige la gracia de 5 s") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    WaitSeconds(Monitor, 4); // 4 s esperando antes del primer datagrama: aun no hay fallback
    Monitor.OnUdpPacket();
    WaitSeconds(Monitor, 5);
    CHECK_FALSE(Monitor.Tick()); // 5 s de espera ya en sesion: con la gracia inicial habria cambiado
    WaitSeconds(Monitor, 4);
    CHECK_FALSE(Monitor.Tick());
    WaitSeconds(Monitor, 1);
    CHECK(Monitor.Tick()); // 10 s
}

TEST_CASE("MR::TransportMonitor: un juego congelado 20 s sin esperar en el socket no cuenta (antes del primer datagrama)") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    // El hilo de recepcion esta bloqueado reenviando al juego: ningun OnUdpWait ni OnUdpPacket,
    // pero el hilo del ping sigue llamando a Tick cada segundo.
    for (int S = 0; S < 20; ++S) {
        CHECK_FALSE(Monitor.Tick());
    }
    CHECK(Monitor.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportMonitor: un juego congelado 20 s sin esperar en el socket no cuenta (en sesion)") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    Monitor.OnUdpPacket();
    WaitSeconds(Monitor, 3);
    for (int S = 0; S < 20; ++S) {
        CHECK_FALSE(Monitor.Tick());
    }
    CHECK(Monitor.Current() == MR::Transport::Udp);
    // El receptor vuelve al socket: solo cuenta lo que espera de verdad (3 s + 6 s = 9 s).
    WaitSeconds(Monitor, 6);
    CHECK_FALSE(Monitor.Tick());
    WaitSeconds(Monitor, 1);
    CHECK(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: una parada no se suma a las esperas (4,9 s + parada + 0,1 s)") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    WaitSeconds(Monitor, 4);
    Monitor.OnUdpWait(milliseconds(900));
    for (int S = 0; S < 30; ++S) {
        CHECK_FALSE(Monitor.Tick());
    }
    Monitor.OnUdpWait(milliseconds(100));
    CHECK(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: diez esperas de un segundo sin datagramas -> TCP (en sesion)") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    Monitor.OnUdpPacket();
    for (int S = 1; S <= 9; ++S) {
        Monitor.OnUdpWait(seconds(1));
        CHECK_FALSE(Monitor.Tick());
    }
    Monitor.OnUdpWait(seconds(1));
    CHECK(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: un datagrama reinicia la cuenta de esperas") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    Monitor.OnUdpPacket();
    WaitSeconds(Monitor, 9);
    CHECK_FALSE(Monitor.Tick());
    Monitor.OnUdpPacket(); // a punto de saltar: el datagrama pone la cuenta a cero
    WaitSeconds(Monitor, 9);
    CHECK_FALSE(Monitor.Tick());
    WaitSeconds(Monitor, 1);
    CHECK(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: un datagrama antes de los 5 s inicia la regla de 10 s") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    WaitSeconds(Monitor, 4);
    Monitor.OnUdpPacket();
    WaitSeconds(Monitor, 9);
    CHECK_FALSE(Monitor.Tick());
    WaitSeconds(Monitor, 1);
    CHECK(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: los errores que vuelven al instante no acumulan espera") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    // Un socket roto: recvfrom falla al momento miles de veces (duracion ~0), ademas de
    // duraciones negativas por si el reloj se comportara mal.
    for (int I = 0; I < 5000; ++I) {
        Monitor.OnUdpWait(milliseconds(0));
        Monitor.OnUdpWait(milliseconds(-50));
        CHECK_FALSE(Monitor.Tick());
    }
    CHECK(Monitor.Current() == MR::Transport::Udp);
    // Las esperas cortas pero reales si suman: 5000 x 1 ms = 5 s antes del primer datagrama.
    for (int I = 0; I < 4999; ++I) {
        Monitor.OnUdpWait(milliseconds(1));
    }
    CHECK_FALSE(Monitor.Tick());
    Monitor.OnUdpWait(milliseconds(1));
    CHECK(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: sin vuelta a UDP") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    WaitSeconds(Monitor, 5);
    REQUIRE(Monitor.Tick());
    Monitor.OnUdpPacket();
    Monitor.OnUdpPacket();
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    Monitor.OnUdpStarted(); // reiniciar el canal tampoco revierte
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: forzado, el primer Tick tras OnUdpStarted da true") {
    MR::TransportMonitor Monitor(true);
    Monitor.OnUdpStarted();
    CHECK(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Monitor.Tick());
}

TEST_CASE("MR::TransportMonitor: forzado, un paquete UDP no evita el TCP") {
    MR::TransportMonitor Monitor(true);
    Monitor.OnUdpStarted();
    Monitor.OnUdpPacket();
    CHECK(Monitor.Tick());
    CHECK(Monitor.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportMonitor: Tick antes de OnUdpStarted es false") {
    MR::TransportMonitor Auto(false);
    Auto.OnUdpWait(seconds(100));
    CHECK_FALSE(Auto.Tick());
    CHECK(Auto.Current() == MR::Transport::Udp);
    MR::TransportMonitor Forced(true);
    CHECK_FALSE(Forced.Tick());
    CHECK(Forced.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportMonitor: dos hilos en el instante del cambio -> exactamente un true") {
    constexpr int Rounds = 300;
    for (int Round = 0; Round < Rounds; ++Round) {
        MR::TransportMonitor Monitor(false);
        Monitor.OnUdpStarted();
        Monitor.OnUdpWait(seconds(5));
        std::atomic<bool> Go { false };
        std::atomic<int> Trues { 0 };
        auto Worker = [&] {
            while (!Go.load()) {
            }
            if (Monitor.Tick()) {
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

TEST_CASE("MR::TransportMonitor: OnUdpPacket y OnUdpWait concurrentes con Tick no rompen el estado") {
    MR::TransportMonitor Monitor(false);
    Monitor.OnUdpStarted();
    std::atomic<bool> Done { false };
    std::thread Receiver([&] {
        // Alterna una espera de 1 s y un datagrama: la espera nunca llega a 5 s.
        for (int S = 1; S <= 2000; ++S) {
            Monitor.OnUdpWait(seconds(1));
            Monitor.OnUdpPacket();
        }
        Done = true;
    });
    int Trues = 0;
    while (!Done.load()) {
        if (Monitor.Tick()) {
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

TEST_CASE("MR::UseSendLarge: con UDP sano conserva la regla de hoy") {
    // Hoy: SendLarge si es ack (O/T) o mide mas de 1000; el resto de TCP va por TCPSend.
    CHECK(MR::UseSendLarge(WithFirst('O', 4), false, MR::Transport::Udp));
    CHECK(MR::UseSendLarge(WithFirst('T', 4), false, MR::Transport::Udp));
    CHECK(MR::UseSendLarge(WithFirst('Z', 1001), false, MR::Transport::Udp));
    CHECK_FALSE(MR::UseSendLarge(WithFirst('C', 600), false, MR::Transport::Udp));
    CHECK_FALSE(MR::UseSendLarge("p", false, MR::Transport::Udp));
    // O/T solo cuentan como ack con mas de 3 bytes (igual que ShouldSendOverTcp).
    CHECK_FALSE(MR::UseSendLarge("Ot", true, MR::Transport::Udp));
}

TEST_CASE("MR::UseSendLarge: en TCP, lo que iba por UDP usa el mismo encuadre que UDPSend") {
    // Paquetes que de otro modo irian por UDP: SendLarge (comprime con ABG: si pasan de 400).
    CHECK(MR::UseSendLarge("p", false, MR::Transport::Tcp));
    CHECK(MR::UseSendLarge(WithFirst('Z', 600), false, MR::Transport::Tcp));
    // Lo que ya iba por TCP conserva su camino: fiable pequeno -> TCPSend, ack o >1000 -> SendLarge.
    CHECK_FALSE(MR::UseSendLarge(WithFirst('C', 600), false, MR::Transport::Tcp));
    CHECK_FALSE(MR::UseSendLarge(WithFirst('Z', 600), true, MR::Transport::Tcp));
    CHECK(MR::UseSendLarge(WithFirst('O', 4), false, MR::Transport::Tcp));
    CHECK(MR::UseSendLarge(WithFirst('Z', 1500), true, MR::Transport::Tcp));
}

TEST_CASE("MR::TransportSession: sin sesion es UDP y no hace nada") {
    MR::TransportSession Session;
    int FtSent = 0;
    CHECK(Session.Current() == MR::Transport::Udp);
    CHECK_FALSE(Session.OnUdpPacket(1));
    Session.OnUdpWait(1, seconds(60));
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 0);
    Session.End(1); // no-op
    CHECK(Session.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportSession: UDP sano anuncia el primer datagrama una sola vez") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Gen = Session.Begin(false, NoFt).Generation;
    CHECK(Gen != 0);
    WaitSeconds(Session, Gen, 1);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(Session.OnUdpPacket(Gen));
    CHECK_FALSE(Session.OnUdpPacket(Gen));
    for (int S = 2; S <= 30; ++S) {
        WaitSeconds(Session, Gen, 1);
        Session.OnUdpPacket(Gen);
        CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    }
    CHECK(FtSent == 0);
    CHECK(Session.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportSession: sin UDP tras 5 s de espera envia Ft una vez y pasa a TCP") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Gen = Session.Begin(false, NoFt).Generation;
    WaitSeconds(Session, Gen, 4);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    WaitSeconds(Session, Gen, 1);
    CHECK(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 1);
    CHECK(Session.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 1);
    // Un datagrama tardio no anuncia "UDP" ni devuelve a UDP.
    CHECK_FALSE(Session.OnUdpPacket(Gen));
    CHECK(Session.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportSession: un juego congelado (sin esperas en el socket) no envia Ft") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Gen = Session.Begin(false, NoFt).Generation;
    CHECK(Session.OnUdpPacket(Gen));
    for (int S = 0; S < 60; ++S) {
        CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    }
    CHECK(FtSent == 0);
    CHECK(Session.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportSession: las esperas de una generacion vieja no cuentan") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Old = Session.Begin(false, NoFt).Generation;
    const auto Fresh = Session.Begin(false, NoFt).Generation;
    WaitSeconds(Session, Old, 60);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    WaitSeconds(Session, Fresh, 5);
    CHECK(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 1);
}

TEST_CASE("MR::TransportSession: Begin sin forzar usa UDP y no envia Ft") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Start = Session.Begin(false, [&] { ++FtSent; });
    CHECK(Start.Generation != 0);
    CHECK(Start.UdpUsed);
    CHECK(FtSent == 0);
    CHECK(Session.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportSession: Begin forzado envia Ft una vez, pasa a TCP y no usa UDP") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Start = Session.Begin(true, [&] { ++FtSent; });
    CHECK(Start.Generation != 0);
    CHECK_FALSE(Start.UdpUsed);
    CHECK(FtSent == 1);
    // Nunca hay una ventana en UDP: no hace falta un Tick para que Current() sea TCP.
    CHECK(Session.Current() == MR::Transport::Tcp);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 1);
    // Un datagrama UDP perdido no anuncia "UDP" ni devuelve a UDP.
    CHECK_FALSE(Session.OnUdpPacket(Start.Generation));
    CHECK(Session.Current() == MR::Transport::Tcp);
}

TEST_CASE("MR::TransportSession: Begin forzado, nadie ve TCP antes de que Ft haya salido") {
    MR::TransportSession Session;
    std::atomic<bool> FtOnWire { false };
    std::atomic<bool> SawTcpBeforeFt { false };
    std::thread Sender;
    const auto Start = Session.Begin(true, [&] {
        Sender = std::thread([&] {
            // Equivale a ServerSend decidiendo la ruta mientras se envia Ft.
            if (Session.Current() == MR::Transport::Tcp && !FtOnWire.load()) {
                SawTcpBeforeFt = true;
            }
        });
        std::this_thread::sleep_for(milliseconds(50));
        FtOnWire = true;
    });
    Sender.join();
    CHECK_FALSE(Start.UdpUsed);
    CHECK_FALSE(SawTcpBeforeFt.load());
}

TEST_CASE("MR::TransportSession: pasar a TCP por el Tick, nadie ve TCP antes de que Ft haya salido") {
    MR::TransportSession Session;
    const auto Gen = Session.Begin(false, NoFt).Generation;
    WaitSeconds(Session, Gen, 5);
    std::atomic<bool> FtOnWire { false };
    std::atomic<bool> SawTcpBeforeFt { false };
    std::thread Sender;
    CHECK(Session.TickAndAnnounce([&] {
        Sender = std::thread([&] {
            if (Session.Current() == MR::Transport::Tcp && !FtOnWire.load()) {
                SawTcpBeforeFt = true;
            }
        });
        std::this_thread::sleep_for(milliseconds(50));
        FtOnWire = true;
    }));
    Sender.join();
    CHECK_FALSE(SawTcpBeforeFt.load());
}

TEST_CASE("MR::TransportSession: una sesion nueva empieza limpia y la anterior no la cierra") {
    MR::TransportSession Session;
    int FtSent = 0;
    const auto Old = Session.Begin(false, NoFt).Generation;
    WaitSeconds(Session, Old, 5);
    CHECK(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(Session.Current() == MR::Transport::Tcp);

    const auto Fresh = Session.Begin(false, NoFt).Generation;
    CHECK(Fresh != Old);
    CHECK(Session.Current() == MR::Transport::Udp); // no hereda el TCP
    CHECK_FALSE(Session.OnUdpPacket(Old)); // datagrama del bucle viejo: ignorado
    CHECK(Session.OnUdpPacket(Fresh));

    Session.End(Old); // el cierre tardio del bucle viejo no toca la sesion nueva
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(Session.Current() == MR::Transport::Udp);
    CHECK(FtSent == 1);

    Session.End(Fresh);
    CHECK(Session.Current() == MR::Transport::Udp);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(FtSent == 1);
}

TEST_CASE("MR::TransportSession: la sesion nueva forzada o no sale del cfg de cada vez") {
    MR::TransportSession Session;
    int FtSent = 0;
    CHECK_FALSE(Session.Begin(true, [&] { ++FtSent; }).UdpUsed);
    CHECK(FtSent == 1);
    CHECK(Session.Begin(false, NoFt).UdpUsed);
    CHECK_FALSE(Session.TickAndAnnounce([&] { ++FtSent; }));
    CHECK(Session.Current() == MR::Transport::Udp);
}

TEST_CASE("MR::ActiveTransport: es una unica instancia compartida") {
    CHECK(&MR::ActiveTransport() == &MR::ActiveTransport());
    CHECK(MR::ActiveTransport().Current() == MR::Transport::Udp);
}

TEST_CASE("MR::TransportCorePacket") {
    CHECK(MR::TransportCorePacket(MR::Transport::Udp) == "Utudp");
    CHECK(MR::TransportCorePacket(MR::Transport::Tcp) == "Uttcp");
}
