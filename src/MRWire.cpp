/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRWire.h"

namespace MR {

bool IsSaneFrameLength(std::int32_t Header) {
    return Header >= 1 && Header <= MaxFrameBytes;
}

}
