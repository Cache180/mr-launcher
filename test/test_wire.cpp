/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRWire.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

TEST_CASE("MR::IsSaneFrameLength: solo 1..MaxFrameBytes") {
    CHECK_FALSE(MR::IsSaneFrameLength(-1));
    CHECK_FALSE(MR::IsSaneFrameLength(0));
    CHECK(MR::IsSaneFrameLength(1));
    CHECK(MR::IsSaneFrameLength(MR::MaxFrameBytes));
    CHECK_FALSE(MR::IsSaneFrameLength(MR::MaxFrameBytes + 1));
    CHECK_FALSE(MR::IsSaneFrameLength(std::numeric_limits<std::int32_t>::min()));
    CHECK_FALSE(MR::IsSaneFrameLength(std::numeric_limits<std::int32_t>::max()));
}

TEST_CASE("MR::MaxFrameBytes: 100 MiB") {
    CHECK(MR::MaxFrameBytes == 100 * 1024 * 1024);
}
