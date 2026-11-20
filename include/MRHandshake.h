/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Saludo `MR` del launcher con el server:
//
//   launcher -> server   MR<version>                 (p. ej. MR1.0)
//   server   -> launcher A                           (version aceptada)
//   launcher -> server   {"name":"...","install":"..."}
//   server   -> launcher P<id>                       (jugador aceptado)
//
// Cualquier rechazo llega como `K<texto>` (o `E<texto>`) y se muestra tal cual.
// Modulo puro: el transporte se inyecta, asi no depende de sockets, de los
// globales ni del logger del launcher y se prueba sin red.
//
// El paquete de identidad lleva la clave de instalacion (credencial): este modulo
// nunca la registra ni la incluye en ningun mensaje.

#include "MRIdentity.h"

#include <cstddef>
#include <functional>
#include <string>

namespace MR {

// Mensaje de estado cuando no hay un nombre de piloto valido guardado.
constexpr const char* NoPilotNameMessage = "Elige un nombre de piloto antes de entrar";

// Tope de la razon de una expulsion que se reenvia al juego (la controla el server).
constexpr std::size_t MaxKickReasonBytes = 256;

struct HandshakeTransport {
    // Envia un paquete con marco. false si el envio fallo.
    std::function<bool(const std::string&)> Send;
    // Recibe un paquete con marco. Vacio si la conexion fallo o se cerro.
    std::function<std::string()> Receive;
};

struct HandshakeResult {
    bool Ok = false;
    int ClientId = -1;
    // Texto para el estado del juego. Vacio si el transporte ya informo del fallo
    // (p. ej. socket cerrado) y no hay nada mejor que decir.
    std::string Failure;
};

HandshakeResult PerformHandshake(const LauncherIdentity& Identity, const std::string& LauncherVersion, const HandshakeTransport& Transport);

}
