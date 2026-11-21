/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRTransport.h"

#include <zlib.h>

namespace MR {

TransportMonitor::TransportMonitor(bool ForceTcp)
    : mForceTcp(ForceTcp) {
}

void TransportMonitor::OnUdpStarted(TimePoint Now) {
    std::scoped_lock Guard(mLock);
    mStarted = true;
    mStart = Now;
    mHavePacket = false;
}

void TransportMonitor::OnUdpPacket(TimePoint Now) {
    std::scoped_lock Guard(mLock);
    mHavePacket = true;
    mLastPacket = Now;
}

bool TransportMonitor::Tick(TimePoint Now) {
    std::scoped_lock Guard(mLock);
    if (mCurrent == Transport::Tcp || !mStarted) {
        return false;
    }
    const bool Switch = mForceTcp
        || (mHavePacket ? Now - mLastPacket >= SessionUdpSilence : Now - mStart >= InitialUdpGrace);
    if (!Switch) {
        return false;
    }
    mCurrent = Transport::Tcp; // sin vuelta a UDP; el true se entrega una sola vez
    return true;
}

Transport TransportMonitor::Current() const {
    std::scoped_lock Guard(mLock);
    return mCurrent;
}

bool ShouldSendOverTcp(std::string_view Data, bool Rel, Transport Current) {
    if (Current == Transport::Tcp) {
        return true;
    }
    // Misma regla que ServerSend (GlobalHandler.cpp): el primer byte solo se mira
    // con mas de 3 bytes; O y T van por el camino grande (ack); N W Y V E C son fiables.
    char C = 0;
    if (Data.size() > 3) {
        C = Data.front();
    }
    const bool Ack = C == 'O' || C == 'T';
    if (C == 'N' || C == 'W' || C == 'Y' || C == 'V' || C == 'E' || C == 'C') {
        Rel = true;
    }
    if (compressBound(static_cast<uLong>(Data.size())) > 1024) {
        Rel = true;
    }
    return Ack || Rel;
}

std::string TransportJson(Transport Current) {
    return Current == Transport::Tcp ? R"({"transport":"tcp"})" : R"({"transport":"udp"})";
}

bool UseSendLarge(std::string_view Data, bool Rel, Transport Current) {
    const bool Ack = Data.size() > 3 && (Data.front() == 'O' || Data.front() == 'T');
    if (Ack || Data.size() > 1000) {
        return true;
    }
    // Solo por el fallback: lo que habria ido por UDP conserva el encuadre de UDPSend.
    return Current == Transport::Tcp && !ShouldSendOverTcp(Data, Rel, Transport::Udp);
}

std::uint64_t TransportSession::Begin(bool ForceTcp, TimePoint Now) {
    std::scoped_lock Guard(mGate);
    mMonitor = std::make_unique<TransportMonitor>(ForceTcp);
    mMonitor->OnUdpStarted(Now);
    mUdpAnnounced = false;
    return ++mGeneration;
}

void TransportSession::End(std::uint64_t Generation) {
    std::scoped_lock Guard(mGate);
    if (Generation == mGeneration) {
        mMonitor.reset();
    }
}

bool TransportSession::OnUdpPacket(std::uint64_t Generation, TimePoint Now) {
    std::scoped_lock Guard(mGate);
    if (!mMonitor || Generation != mGeneration) {
        return false;
    }
    mMonitor->OnUdpPacket(Now);
    if (mUdpAnnounced || mMonitor->Current() == Transport::Tcp) {
        return false;
    }
    mUdpAnnounced = true;
    return true;
}

bool TransportSession::TickAndAnnounce(TimePoint Now, const std::function<void()>& SendFt) {
    std::scoped_lock Guard(mGate);
    if (!mMonitor || !mMonitor->Tick(Now)) {
        return false;
    }
    SendFt();
    return true;
}

Transport TransportSession::Current() const {
    std::scoped_lock Guard(mGate);
    return mMonitor ? mMonitor->Current() : Transport::Udp;
}

TransportSession& ActiveTransport() {
    static TransportSession Instance;
    return Instance;
}

}
