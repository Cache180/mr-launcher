/*
 Copyright (C) 2026 Muertazos Racing.
 Licensed under AGPL-3.0 (or later), see <https://www.gnu.org/licenses/>.
 SPDX-License-Identifier: AGPL-3.0-or-later
*/

#include "MRIdentity.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <nlohmann/json.hpp>
#include <openssl/rand.h>
#include <stdexcept>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace MR {

fs::path ConfigPath(const fs::path& ExeDir) {
    return ExeDir / ConfigFileName;
}

namespace {

    // ordered_json conserva el orden de las claves al reescribir el archivo.
    using Json = nlohmann::ordered_json;

    constexpr std::size_t NameMin = 3;
    constexpr std::size_t NameMax = 20;
    constexpr std::size_t InstallMin = 32;
    constexpr std::size_t InstallMax = 64;
    constexpr std::size_t GeneratedKeyBytes = 24; // 48 caracteres hex
    constexpr std::uintmax_t MaxConfigBytes = 1024 * 1024;
    constexpr const char* NameReason = "nombre invalido (3-20 caracteres: letras, numeros, _ . -)";
    constexpr const char* SaveReason = "no se pudo guardar la configuracion";

    // Solo ASCII y sin funciones dependientes de la configuracion regional:
    // mismo comportamiento que el server.
    bool IsAlnum(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }

    bool IsValidPort(const Json& Value) {
        if (!Value.is_number_integer()) {
            return false;
        }
        const auto Port = Value.get<std::int64_t>();
        return Port >= 1 && Port <= 65535;
    }

    constexpr const char* TransportAuto = "auto";
    constexpr const char* TransportTcp = "tcp";

    // "Transport": "auto" o "tcp"; cualquier otra cosa (incluido otro tipo JSON) es invalida.
    template <typename BasicJson>
    bool IsValidTransportValue(const BasicJson& Value) {
        return Value.is_string() && (Value.template get_ref<const std::string&>() == TransportAuto || Value.template get_ref<const std::string&>() == TransportTcp);
    }

    enum class ReadKind {
        Missing, // el archivo no existe: se crea
        Unreadable, // existe pero no se pudo leer ahora: NO se escribe nada
        Content // se leyo entero (puede ser invalido)
    };

    struct ReadResult {
        ReadKind Kind = ReadKind::Missing;
        Json Doc = Json::object(); // solo con Valid
        bool Valid = false; // objeto JSON bien formado
        bool NonEmpty = false; // hay contenido que merece copia de seguridad
    };

    constexpr int ReadAttempts = 5;
    constexpr std::chrono::milliseconds ReadRetryDelay { 50 };

    ReadResult ReadOnce(const fs::path& File) {
        ReadResult Result;
        std::error_code Ec;
        const fs::file_status Status = fs::status(File, Ec);
        if (Status.type() == fs::file_type::not_found) {
            return Result; // Missing
        }
        Result.Kind = ReadKind::Unreadable;
        if (Ec || Status.type() != fs::file_type::regular) {
            return Result;
        }
        const auto Size = fs::file_size(File, Ec);
        if (Ec) {
            return Result;
        }
        if (Size == 0) {
            Result.Kind = ReadKind::Content; // nada que conservar
            return Result;
        }
        if (Size > MaxConfigBytes) {
            Result.Kind = ReadKind::Content;
            Result.NonEmpty = true;
            return Result;
        }
        std::ifstream In(File, std::ios::binary);
        if (!In) {
            return Result;
        }
        std::string Buffer(static_cast<std::size_t>(Size), '\0');
        In.read(Buffer.data(), static_cast<std::streamsize>(Buffer.size()));
        if (static_cast<std::uintmax_t>(In.gcount()) != Size) {
            return Result; // lectura corta: se reintenta
        }
        Result.Kind = ReadKind::Content;
        Result.NonEmpty = true;
        Json Doc = Json::parse(Buffer, nullptr, false);
        if (!Doc.is_discarded() && Doc.is_object()) {
            Result.Doc = std::move(Doc);
            Result.Valid = true;
        }
        return Result;
    }

    // Tres desenlaces (ver ReadKind). Unreadable se reintenta unas veces antes de rendirse.
    ReadResult ReadConfig(const fs::path& File) {
        ReadResult Result;
        for (int Attempt = 0; Attempt < ReadAttempts; ++Attempt) {
            if (Attempt > 0) {
                std::this_thread::sleep_for(ReadRetryDelay);
            }
            Result = ReadOnce(File);
            if (Result.Kind != ReadKind::Unreadable) {
                break;
            }
        }
        return Result;
    }

    // Copia el contenido viejo a <cfg>.corrupt (sobrescribe una copia anterior).
    bool BackupCorrupt(const fs::path& File) {
        std::error_code Ec;
        fs::path Backup = File;
        Backup += ".corrupt";
        fs::copy_file(File, Backup, fs::copy_options::overwrite_existing, Ec);
        return !Ec;
    }

    unsigned long CurrentPid() {
#ifdef _WIN32
        return static_cast<unsigned long>(GetCurrentProcessId());
#else
        return static_cast<unsigned long>(getpid());
#endif
    }

    // Vuelca a disco el archivo ya escrito y cerrado (FlushFileBuffers / fsync).
    bool SyncFileToDisk(const fs::path& File) {
#ifdef _WIN32
        HANDLE H = CreateFileW(File.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (H == INVALID_HANDLE_VALUE) {
            return false;
        }
        const bool Ok = FlushFileBuffers(H) != 0;
        CloseHandle(H);
        return Ok;
#else
        const int Fd = open(File.c_str(), O_RDONLY);
        if (Fd < 0) {
            return false;
        }
        const bool Ok = fsync(Fd) == 0;
        close(Fd);
        return Ok;
#endif
    }

    // Renombra Tmp sobre File reemplazando el destino; en Windows con WRITE_THROUGH
    // (no vuelve hasta que el renombrado esta en disco).
    bool ReplaceFile(const fs::path& Tmp, const fs::path& File) {
#ifdef _WIN32
        return MoveFileExW(Tmp.c_str(), File.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        std::error_code Ec;
        fs::rename(Tmp, File, Ec);
        return !Ec;
#endif
    }

    // Escritura atomica: archivo temporal propio de este proceso junto al destino,
    // volcado a disco y renombrado. Ver detail::WriteConfigAtomicTo.
    bool WriteConfigAtomic(const fs::path& File, const Json& Doc) {
        return detail::WriteConfigAtomicTo(File, Doc, detail::TempConfigPath(File, CurrentPid()));
    }

    // Completa y corrige las claves que posee el launcher; el resto no se toca.
    // Devuelve true si Doc cambio. Puede lanzar solo por el generador de claves,
    // y la clave se genera al final: si falla, Doc queda sin Install valido.
    bool RepairConfig(Json& Doc, const detail::KeyGenerator& Generate) {
        bool Changed = false;

        auto Port = Doc.find("Port");
        if (Port == Doc.end() || !IsValidPort(*Port)) {
            Doc["Port"] = DefaultPort;
            Changed = true;
        }
        auto Build = Doc.find("Build");
        if (Build == Doc.end() || !Build->is_string()) {
            Doc["Build"] = "Default";
            Changed = true;
        }
        if (!Doc.contains("CachingDirectory")) {
            Doc["CachingDirectory"] = "./Resources";
            Changed = true;
        }
        auto Name = Doc.find("Name");
        if (Name == Doc.end() || !Name->is_string() || (!Name->get_ref<const std::string&>().empty() && !IsValidPilotName(Name->get_ref<const std::string&>()))) {
            Doc["Name"] = "";
            Changed = true;
        }
        auto Servers = Doc.find("Servers");
        if (Servers == Doc.end() || !Servers->is_array()) {
            Doc["Servers"] = Json::array();
            Changed = true;
        }
        auto Transport = Doc.find("Transport");
        if (Transport == Doc.end() || !IsValidTransportValue(*Transport)) {
            Doc["Transport"] = TransportAuto;
            Changed = true;
        }
        auto Install = Doc.find("Install");
        if (Install == Doc.end() || !Install->is_string() || !IsValidInstallKey(Install->get_ref<const std::string&>())) {
            std::string Key = Generate();
            if (!IsValidInstallKey(Key)) {
                throw std::runtime_error("clave de instalacion invalida");
            }
            Doc["Install"] = std::move(Key);
            Changed = true;
        }
        return Changed;
    }

    // Lo que hay en Doc, descartando lo invalido (Install vacio si no hay clave valida).
    LauncherIdentity ExtractIdentity(const Json& Doc) {
        LauncherIdentity Id;
        auto Name = Doc.find("Name");
        if (Name != Doc.end() && Name->is_string() && IsValidPilotName(Name->get_ref<const std::string&>())) {
            Id.Name = Name->get<std::string>();
        }
        auto Install = Doc.find("Install");
        if (Install != Doc.end() && Install->is_string() && IsValidInstallKey(Install->get_ref<const std::string&>())) {
            Id.Install = Install->get<std::string>();
        }
        return Id;
    }

    enum class Prepare {
        Ok,
        Unreadable, // el cfg existe pero no se pudo leer: no se escribio nada
        BackupFailed, // no se pudo copiar el contenido viejo a .corrupt: no se escribio nada
        NoKey // no hay clave de instalacion valida (RNG caido): no se escribio nada
    };

    // Lee y repara el cfg en memoria (Doc, Changed). Antes de descartar contenido
    // que tenia algo que perder (ilegible como JSON, o sin Install valido) lo
    // copia a <cfg>.corrupt. No escribe el cfg.
    Prepare PrepareConfig(const fs::path& File, const detail::KeyGenerator& Generate, Json& Doc, bool& Changed) {
        ReadResult Read = ReadConfig(File);
        if (Read.Kind == ReadKind::Unreadable) {
            return Prepare::Unreadable;
        }
        Doc = Read.Valid ? std::move(Read.Doc) : Json::object();
        if (Read.NonEmpty && ExtractIdentity(Doc).Install.empty() && !BackupCorrupt(File)) {
            return Prepare::BackupFailed;
        }
        try {
            Changed = RepairConfig(Doc, Generate);
        } catch (...) {
            return Prepare::NoKey;
        }
        return Prepare::Ok;
    }

    // Guarda Name (ya validado, o "" para borrarlo) en el cfg. Unica via de
    // escritura del nombre: SetPilotName y ClearPilotName pasan por aqui.
    bool StoreName(const fs::path& CfgFile, std::string_view Name, std::string& Reason) {
        try {
            Json Doc = Json::object();
            bool Changed = false;
            switch (PrepareConfig(CfgFile, GenerateInstallKey, Doc, Changed)) {
            case Prepare::Ok:
                break;
            case Prepare::Unreadable:
                Reason = "no se pudo leer " + CfgFile.filename().string() + ": cierra lo que lo este usando y vuelve a intentarlo";
                return false;
            case Prepare::BackupFailed:
            case Prepare::NoKey:
                Reason = SaveReason;
                return false;
            }
            Doc["Name"] = std::string(Name);
            if (!WriteConfigAtomic(CfgFile, Doc)) {
                Reason = SaveReason;
                return false;
            }
            Reason.clear();
            return true;
        } catch (...) {
            Reason = SaveReason;
            return false;
        }
    }

}

namespace {
    template <typename BasicJson>
    bool ForceTcpImpl(const BasicJson& Doc) {
        if (!Doc.is_object()) {
            return false;
        }
        const auto Transport = Doc.find("Transport");
        return Transport != Doc.end() && Transport->is_string() && Transport->template get_ref<const std::string&>() == TransportTcp;
    }
}

bool ForceTcpFromConfig(const nlohmann::json& Doc) {
    return ForceTcpImpl(Doc);
}

bool ForceTcpFromConfig(const nlohmann::ordered_json& Doc) {
    return ForceTcpImpl(Doc);
}

bool IsValidPilotName(std::string_view Name) {
    if (Name.size() < NameMin || Name.size() > NameMax) {
        return false;
    }
    for (char c : Name) {
        if (!(IsAlnum(c) || c == '_' || c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

bool IsValidInstallKey(std::string_view Install) {
    if (Install.size() < InstallMin || Install.size() > InstallMax) {
        return false;
    }
    for (char c : Install) {
        if (!IsAlnum(c)) {
            return false;
        }
    }
    return true;
}

std::string GenerateInstallKey() {
    unsigned char Bytes[GeneratedKeyBytes];
    if (RAND_bytes(Bytes, static_cast<int>(sizeof(Bytes))) != 1) {
        throw std::runtime_error("no se pudo obtener aleatoriedad criptografica");
    }
    static constexpr char Hex[] = "0123456789abcdef";
    std::string Key;
    Key.reserve(GeneratedKeyBytes * 2);
    for (unsigned char B : Bytes) {
        Key += Hex[B >> 4];
        Key += Hex[B & 0x0F];
    }
    return Key;
}

namespace detail {

    fs::path TempConfigPath(const fs::path& File, unsigned long Pid) {
        fs::path Tmp = File;
        Tmp += "." + std::to_string(Pid) + ".tmp";
        return Tmp;
    }

    // El renombrado reemplaza el destino, asi que un corte deja el cfg viejo o el
    // nuevo, nunca uno a medias. El temporal se vuelca a disco antes de renombrar y
    // no queda ningun *.tmp ni al terminar bien ni al fallar.
    bool WriteConfigAtomicTo(const fs::path& File, const Json& Doc, const fs::path& Tmp) {
        std::error_code Ec;
        try {
            const fs::path Parent = File.parent_path();
            if (!Parent.empty()) {
                fs::create_directories(Parent, Ec);
            }
            {
                std::ofstream Out(Tmp, std::ios::binary | std::ios::trunc);
                if (!Out) {
                    return false;
                }
                Out << Doc.dump(4, ' ', false, Json::error_handler_t::replace) << '
';
                Out.flush();
                if (!Out) {
                    Out.close();
                    fs::remove(Tmp, Ec);
                    return false;
                }
            }
            if (!SyncFileToDisk(Tmp) || !ReplaceFile(Tmp, File)) {
                fs::remove(Tmp, Ec);
                return false;
            }
            return true;
        } catch (...) {
            fs::remove(Tmp, Ec);
            return false;
        }
    }

    LauncherIdentity LoadOrCreateIdentityWith(const fs::path& CfgFile, const KeyGenerator& Generate) {
        try {
            Json Doc = Json::object();
            bool Changed = false;
            switch (PrepareConfig(CfgFile, Generate, Doc, Changed)) {
            case Prepare::Ok:
                break;
            case Prepare::NoKey:
                return ExtractIdentity(Doc); // Install vacio
            case Prepare::Unreadable:
            case Prepare::BackupFailed:
                return {};
            }
            // Nunca se devuelve una clave que no este en disco: si no se pudo
            // guardar, Install vacio (ConfigInit lo trata como error fatal).
            if (Changed && !WriteConfigAtomic(CfgFile, Doc)) {
                return {};
            }
            return ExtractIdentity(Doc);
        } catch (...) {
            return {};
        }
    }

}

LauncherIdentity LoadOrCreateIdentity(const fs::path& CfgFile) {
    return detail::LoadOrCreateIdentityWith(CfgFile, GenerateInstallKey);
}

bool SetPilotName(const fs::path& CfgFile, std::string_view Name, std::string& Reason) {
    if (!IsValidPilotName(Name)) {
        Reason = NameReason;
        return false;
    }
    return StoreName(CfgFile, Name, Reason);
}

bool ClearPilotName(const fs::path& CfgFile, std::string& Reason) {
    return StoreName(CfgFile, {}, Reason);
}

std::string IdentityPacket(const LauncherIdentity& Identity) {
    Json Packet = Json::object();
    Packet["name"] = Identity.Name;
    Packet["install"] = Identity.Install;
    return Packet.dump(-1, ' ', false, Json::error_handler_t::replace);
}

}
