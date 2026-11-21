/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRPaths.h"

namespace MR {

std::filesystem::path ExeDir() {
    return {};
}

std::filesystem::path ResolveCacheDir(const std::filesystem::path& Dir, const std::string&) {
    return Dir;
}

}
