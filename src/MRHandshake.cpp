/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRHandshake.h"

#include <string_view>

namespace MR {

namespace {

    // El id de P<id> cabe en un int con holgura: 9 digitos como maximo.
    constexpr std::size_t MaxIdDigits = 9;
    // El server nunca manda una identidad de mas de 256 bytes; lo mismo aplica al envio.
    constexpr std::size_t MaxIdentityBytes = 256;

    constexpr const char* InvalidIdentityMessage = "Identidad local invalida: reinicia el launcher";
    constexpr const char* RejectedMessage = "Conexion rechazada por el server";
    constexpr const char* UnexpectedReplyMessage = "Respuesta inesperada del server";

    HandshakeResult Fail(std::string Failure = {}) {
        HandshakeResult Result;
        Result.Failure = std::move(Failure);
        return Result;
    }

    // Texto de una expulsion (`K...` o `E...`) acotado a MaxKickReasonBytes sin
    // partir un caracter UTF-8 multibyte.
    std::string KickReason(std::string_view Reply) {
        std::string_view Text = Reply.substr(1);
        if (Text.size() > MaxKickReasonBytes) {
            std::size_t Cut = MaxKickReasonBytes;
            // Un byte de continuacion (10xxxxxx) en Cut significa que el caracter
            // empezo antes: se descarta entero.
            while (Cut > 0 && (static_cast<unsigned char>(Text[Cut]) & 0xC0) == 0x80) {
                --Cut;
            }
            Text = Text.substr(0, Cut);
        }
        return Text.empty() ? std::string(RejectedMessage) : std::string(Text);
    }

    bool IsRejection(const std::string& Reply) {
        return !Reply.empty() && (Reply[0] == 'K' || Reply[0] == 'E');
    }

    // Si la respuesta no es la esperada, el resultado de fallo que corresponde.
    // Vacio = el transporte ya informo (conexion caida).
    HandshakeResult Unexpected(const std::string& Reply) {
        if (Reply.empty()) {
            return Fail();
        }
        if (IsRejection(Reply)) {
            return Fail(KickReason(Reply));
        }
        return Fail(UnexpectedReplyMessage);
    }

    bool ParseClientId(const std::string& Reply, int& Id) {
        if (Reply.size() < 2 || Reply[0] != 'P' || Reply.size() - 1 > MaxIdDigits) {
            return false;
        }
        int Value = 0;
        for (std::size_t I = 1; I < Reply.size(); ++I) {
            if (Reply[I] < '0' || Reply[I] > '9') {
                return false;
            }
            Value = Value * 10 + (Reply[I] - '0');
        }
        Id = Value;
        return true;
    }

} // namespace

HandshakeResult PerformHandshake(const LauncherIdentity& Identity, const std::string& LauncherVersion, const HandshakeTransport& Transport) {
    if (!IsValidPilotName(Identity.Name)) {
        return Fail(NoPilotNameMessage);
    }
    if (!IsValidInstallKey(Identity.Install)) {
        return Fail(InvalidIdentityMessage);
    }
    const std::string Packet = IdentityPacket(Identity);
    if (Packet.size() > MaxIdentityBytes) {
        return Fail(InvalidIdentityMessage);
    }

    if (!Transport.Send("MR" + LauncherVersion)) {
        return Fail();
    }
    const std::string Accepted = Transport.Receive();
    if (Accepted != "A") {
        return Unexpected(Accepted);
    }

    if (!Transport.Send(Packet)) {
        return Fail();
    }
    const std::string Reply = Transport.Receive();
    HandshakeResult Result;
    if (!ParseClientId(Reply, Result.ClientId)) {
        return Unexpected(Reply);
    }
    Result.Ok = true;
    return Result;
}

}
