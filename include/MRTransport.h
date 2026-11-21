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
#include <cstdint>
#include <functional>
#include <memory>
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

// Paquete del canal del core launcher -> juego que anuncia el transporte: "Utudp" / "Uttcp".
std::string TransportCorePacket(Transport Current);

// Una vez elegido TCP (ShouldSendOverTcp), decide entre SendLarge (true) y TCPSend (false).
// Regla de hoy: SendLarge si es ack (O/T con mas de 3 bytes) o mide mas de 1000 bytes.
// Ademas, con Transport::Tcp, lo que sin el fallback habria ido por UDP usa SendLarge: asi
// lleva el mismo encuadre que UDPSend (comprimido con ABG: si pasa de 400 bytes), que el
// server acepta por TCP tras autenticar.
bool UseSendLarge(std::string_view Data, bool Rel, Transport Current);

// Monitor de la sesion activa del launcher, accesible desde los hilos que intervienen
// (recepcion UDP, ping, emisores y proxy HTTP). Todo el estado vive tras un unico candado,
// asi que no escapa ningun puntero: no hay uso-tras-liberar al terminar una sesion.
class TransportSession {
public:
    using TimePoint = TransportMonitor::TimePoint;

    struct SessionStart {
        std::uint64_t Generation; // identifica a quien abrio la sesion (para End y OnUdpPacket)
        bool UdpUsed; // false: la sesion va solo por TCP; no se usa el canal UDP
    };

    // Empieza una sesion nueva (descarta por completo la anterior, incluido su TCP) y
    // arranca su canal UDP. Con ForceTcp pasa a TCP DENTRO de este mismo candado: llama a
    // SendFt antes de que nadie pueda ver Transport::Tcp, no hay ventana en UDP y
    // UdpUsed es false (el llamador no envia ni recibe nada por UDP).
    SessionStart Begin(bool ForceTcp, TimePoint Now, const std::function<void()>& SendFt);
    // Cierra la sesion si Generation sigue siendo la activa; si ya hay otra mas nueva, no hace nada.
    void End(std::uint64_t Generation);
    // Llego un datagrama UDP del server. true solo para el primero de la sesion mientras
    // siga en UDP (el llamador registra "Conexion: UDP"). Una generacion vieja se ignora.
    bool OnUdpPacket(std::uint64_t Generation, TimePoint Now);
    // Tick del monitor. Si hay que pasar a TCP, llama a SendFt (envio de Ft por TCP) SIN soltar
    // el candado: quien consulte Current() espera a que Ft haya salido, asi ningun paquete
    // se encamina por TCP antes que Ft. true solo en el instante del cambio. Sin sesion: false.
    bool TickAndAnnounce(TimePoint Now, const std::function<void()>& SendFt);
    // Sin sesion activa: Udp.
    Transport Current() const;

private:
    mutable std::mutex mGate;
    std::unique_ptr<TransportMonitor> mMonitor;
    std::uint64_t mGeneration = 0;
    bool mUdpAnnounced = false;
};

// Instancia unica del launcher (una sesion de server a la vez).
TransportSession& ActiveTransport();

}
