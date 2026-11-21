/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Decisiones puras sobre el encuadre de las tramas TCP del server. Modulo puro:
// sin sockets, globales ni logger.

#include <cstdint>

namespace MR {

// Tamano maximo aceptado para una trama TCP del server (100 MiB).
inline constexpr std::int32_t MaxFrameBytes = 100 * 1024 * 1024;

// true solo para una cabecera de longitud entre 1 y MaxFrameBytes. Una trama
// negativa, vacia o desmesurada no se reserva: se cierra la conexion.
bool IsSaneFrameLength(std::int32_t Header);

}
