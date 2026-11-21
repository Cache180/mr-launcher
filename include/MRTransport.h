/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Decision de transporte del launcher: UDP o solo TCP. Modulo puro: no depende de
// los globales, del logger ni de los sockets del launcher, y nunca lee el reloj
// (el llamador inyecta la hora), para poder probarlo de forma determinista.
//
// Un monitor por sesion: una vez que pasa a TCP no vuelve a UDP.

#include <chrono>
#include <mutex>
#include <string>
#include <string_view>

namespace MR {

enum class Transport { Udp,
    Tcp };

// Desde que arranca el canal UDP hasta que debe llegar el primer paquete UDP del server.
inline constexpr auto InitialUdpGrace = std::chrono::seconds(5);
// Silencio UDP maximo tolerado ya en sesion (con el TCP vivo).
inline constexpr auto SessionUdpSilence = std::chrono::seconds(10);

class TransportMonitor {
public:
    using TimePoint = std::chrono::steady_clock::time_point;

    // ForceTcp: el primer Tick tras OnUdpStarted pasa a TCP sin esperar.
    explicit TransportMonitor(bool ForceTcp);

    // Arranca el canal UDP: desde Now corre la gracia inicial. No revierte un TCP ya decidido.
    void OnUdpStarted(TimePoint Now);
    // Llego un paquete UDP del server (hilo de recepcion UDP).
    void OnUdpPacket(TimePoint Now);
    // Llamar periodicamente (hilo del ping, 1 Hz). Devuelve true UNA sola vez, en
    // el instante en que hay que pasar a TCP; antes de OnUdpStarted siempre false.
    bool Tick(TimePoint Now);
    Transport Current() const;

private:
    mutable std::mutex mLock;
    const bool mForceTcp;
    bool mStarted = false;
    bool mHavePacket = false;
    TimePoint mStart {};
    TimePoint mLastPacket {};
    Transport mCurrent = Transport::Udp;
};

// Mismas reglas que ServerSend (GlobalHandler.cpp) para elegir TCP frente a UDP:
// con Transport::Udp reproduce exactamente la decision actual; con Transport::Tcp
// siempre es true.
bool ShouldSendOverTcp(std::string_view Data, bool Rel, Transport Current);

// {"transport":"udp"} / {"transport":"tcp"}
std::string TransportJson(Transport Current);

}
