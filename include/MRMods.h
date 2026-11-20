/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

// Instalacion del mod multijugador desde el zip que viaja junto al launcher
// (MRMP.zip). Modulo puro: no depende de los globales ni del logger del
// launcher, para poder enlazarse en el binario de pruebas.

#include <filesystem>
#include <string>

namespace MR {

// Borra todo archivo de Dir que no sea mrmp.zip (sin distinguir mayusculas).
// No toca subcarpetas. Un Dir inexistente no es un error. Lanza
// std::filesystem::filesystem_error si no puede borrar algo (el juego abierto,
// un archivo en uso).
void CleanMultiplayerDir(const std::filesystem::path& Dir);

// Copia ExeDir/MRMP.zip a GameUserPath/mods/multiplayer/MRMP.zip solo si falta o
// su sha256 difiere. La copia es atomica (temporal + renombrado). Devuelve falso
// con el motivo en Error si no hay zip junto al ejecutable o no se pudo instalar.
// Nunca lanza.
bool InstallBundledMod(const std::filesystem::path& ExeDir, const std::filesystem::path& GameUserPath, std::string& Error);

}
