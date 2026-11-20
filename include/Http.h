/*
 Copyright (C) 2024 BeamMP Ltd., BeamMP team and contributors.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#pragma once

class HTTP {
public:
    // Servidor HTTP local que el mod usa para descubrir su puerto (mensaje `P`).
    // No sale a internet: responde 404 a todo.
    static void StartProxy();
};
