// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "DeferredRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>
#include <system_error>
#include <unordered_map>
#if defined(_WIN32)
#include <winhttp.h>
#endif

namespace {
std::string ResolveWebSnapshotPath() {
    const char* envPath = std::getenv("NDEVC_WEB_SNAPSHOT_PATH");
    if (envPath && envPath[0] != '\0') {
        return std::string(envPath);
    }
    return (std::filesystem::path(SOURCE_DIR) / "src" / "web" / "content" / "data" / "runtime_snapshot.json").string();
}

std::string JsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char c : text) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[7];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

void JsonWriteString(std::ostringstream& os, std::string_view text) {
    os << "\"" << JsonEscape(text) << "\"";
}

std::string UtcNowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tmUtc{};
#if defined(_WIN32)
    gmtime_s(&tmUtc, &t);
#else
    gmtime_r(&t, &tmUtc);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmUtc);
    return std::string(buf);
}

void JsonWriteVec3(std::ostringstream& os, const glm::vec3& v) {
    os << "[" << v.x << "," << v.y << "," << v.z << "]";
}

void JsonWriteVec4(std::ostringstream& os, const glm::vec4& v) {
    os << "[" << v.x << "," << v.y << "," << v.z << "," << v.w << "]";
}

struct SnapshotMeshInfo {
    const Mesh* mesh = nullptr;
    std::string meshResourceId;
    std::string meshPath;
    size_t drawRefCount = 0;
};

std::string BuildMeshPathFromResourceId(const std::string& meshResourceId) {
    if (meshResourceId.empty()) return {};
    std::string actualPath = meshResourceId;
    if (actualPath.starts_with("msh:")) {
        actualPath = actualPath.substr(4);
    }

    std::replace(actualPath.begin(), actualPath.end(), '\\', '/');
    if (!actualPath.ends_with(".nvx2")) {
        actualPath += ".nvx2";
    }
    return std::string(MESHES_ROOT) + actualPath;
}

std::string ReadEnvString(const char* name) {
    if (!name || !name[0]) return {};
#if defined(_WIN32)
    char* value = nullptr;
    size_t len = 0;
    if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
        return {};
    }
    std::string out(value);
    std::free(value);
    return out;
#else
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
#endif
}

bool ReadEnvToggle(const char* name) {
    const std::string value = ReadEnvString(name);
    return !value.empty() && value != "0" && value != "false" && value != "FALSE";
}

bool WebAddonEnabled() {
    const std::string enabled = ReadEnvString("NDEVC_WEB_ADDON_ENABLED");
    if (!enabled.empty()) {
        return enabled != "0" && enabled != "false" && enabled != "FALSE";
    }
    return !ReadEnvToggle("NDEVC_WEB_ADDON_DISABLE");
}

std::string ResolveRuntimeImportUrl() {
    const std::string envUrl = ReadEnvString("NDEVC_WEB_IMPORT_URL");
    if (!envUrl.empty()) {
        return envUrl;
    }
    return "http://localhost:5164/api/runtime/import";
}

std::string UrlOrigin(std::string_view url) {
    const size_t schemePos = url.find("://");
    if (schemePos == std::string_view::npos) return std::string(url);
    const size_t hostStart = schemePos + 3;
    const size_t pathPos = url.find('/', hostStart);
    if (pathPos == std::string_view::npos) return std::string(url);
    return std::string(url.substr(0, pathPos));
}

std::string EnsureTrailingSlash(std::string value) {
    if (!value.empty() && value.back() != '/') value.push_back('/');
    return value;
}

std::string ResolveWebApiBaseUrl() {
    const std::string envBase = ReadEnvString("NDEVC_WEB_API_BASE");
    if (!envBase.empty()) {
        if (envBase.find("/api/runtime/import") != std::string::npos ||
            envBase.find("/api/runtime/events") != std::string::npos) {
            return UrlOrigin(envBase);
        }
        return envBase;
    }
    return "http://localhost:5164";
}

std::string ResolveRuntimeEventsUrl() {
    const std::string envUrl = ReadEnvString("NDEVC_WEB_EVENTS_URL");
    if (!envUrl.empty()) {
        return envUrl;
    }
    return ResolveWebApiBaseUrl() + "/api/runtime/events";
}

std::string ResolveCdnBaseUrl() {
    const std::string envUrl = ReadEnvString("NDEVC_WEB_CDN_URL");
    if (!envUrl.empty()) {
        if (envUrl.find("/api/runtime/import") != std::string::npos ||
            envUrl.find("/api/runtime/events") != std::string::npos) {
            return EnsureTrailingSlash(UrlOrigin(envUrl) + "/cdndata");
        }
        if (envUrl.find("/cdndata") == std::string::npos) {
            return EnsureTrailingSlash(envUrl) + "cdndata/";
        }
        return EnsureTrailingSlash(envUrl);
    }
    return EnsureTrailingSlash(ResolveWebApiBaseUrl() + "/cdndata");
}

std::string UrlEncodePath(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 16);
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~' || c == '/') {
            out.push_back(static_cast<char>(c));
        } else {
            char buf[4];
            std::snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned int>(c));
            out += buf;
        }
    }
    return out;
}

std::string MakeCdnGetUrlForLocalPath(const std::string& localPath) {
    if (localPath.empty()) return {};

    const std::filesystem::path sourceRoot(SOURCE_DIR);
    std::filesystem::path inputPath(localPath);
    if (inputPath.empty()) return {};

    std::error_code ec;
    if (!inputPath.is_absolute()) {
        inputPath = std::filesystem::absolute(inputPath, ec);
        if (ec) return {};
    }

    std::filesystem::path rel = std::filesystem::relative(inputPath, sourceRoot, ec);
    if (ec || rel.empty()) {
        rel = inputPath.filename();
    }

    std::string relUtf8 = rel.generic_string();
    if (relUtf8.empty()) return {};
    while (!relUtf8.empty() && (relUtf8[0] == '/' || relUtf8[0] == '\\')) {
        relUtf8.erase(relUtf8.begin());
    }

    std::string base = ResolveCdnBaseUrl();
    if (!base.empty() && base.back() != '/') {
        base.push_back('/');
    }
    return base + UrlEncodePath(relUtf8);
}

#if defined(_WIN32)
std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (required <= 1) return {};
    std::wstring out(static_cast<size_t>(required - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), required);
    return out;
}

struct HttpEndpoint {
    bool valid = false;
    bool secure = false;
    INTERNET_PORT port = 0;
    std::wstring host;
    std::wstring path;
};

HttpEndpoint ParseHttpEndpoint(const std::string& url) {
    HttpEndpoint endpoint{};
    std::wstring wurl = Utf8ToWide(url);
    if (wurl.empty()) return endpoint;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
        return endpoint;
    }

    if (uc.nScheme != INTERNET_SCHEME_HTTP && uc.nScheme != INTERNET_SCHEME_HTTPS) {
        return endpoint;
    }

    endpoint.secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    endpoint.port = uc.nPort;
    endpoint.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    endpoint.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength > 0 && uc.lpszExtraInfo) {
        endpoint.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    }
    if (endpoint.path.empty()) endpoint.path = L"/";
    endpoint.valid = !endpoint.host.empty();
    return endpoint;
}

bool PostJson(const std::string& url, const std::string& payload, std::string* errorOut = nullptr) {
    const HttpEndpoint endpoint = ParseHttpEndpoint(url);
    if (!endpoint.valid) {
        if (errorOut) *errorOut = "Invalid URL";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"NDEVC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (errorOut) *errorOut = "WinHttpOpen failed";
        return false;
    }

    WinHttpSetTimeouts(session, 150, 150, 300, 300);

    HINTERNET connect = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (!connect) {
        if (errorOut) *errorOut = "WinHttpConnect failed";
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"POST", endpoint.path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        if (errorOut) *errorOut = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    static const wchar_t* headers = L"Content-Type: application/json\r\n";
    const DWORD payloadSize = static_cast<DWORD>(payload.size());
    const BOOL sent = WinHttpSendRequest(
        request,
        headers,
        static_cast<DWORD>(-1),
        payloadSize ? const_cast<char*>(payload.data()) : nullptr,
        payloadSize,
        payloadSize,
        0);
    if (!sent) {
        if (errorOut) *errorOut = "WinHttpSendRequest failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        if (errorOut) *errorOut = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode,
                             &statusCodeSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        if (errorOut) *errorOut = "WinHttpQueryHeaders failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (statusCode < 200 || statusCode >= 300) {
        if (errorOut) *errorOut = "HTTP status " + std::to_string(statusCode);
        return false;
    }

    return true;
}

bool GetUrl(const std::string& url, std::string* errorOut = nullptr) {
    const HttpEndpoint endpoint = ParseHttpEndpoint(url);
    if (!endpoint.valid) {
        if (errorOut) *errorOut = "Invalid URL";
        return false;
    }

    HINTERNET session = WinHttpOpen(L"NDEVC/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        if (errorOut) *errorOut = "WinHttpOpen failed";
        return false;
    }

    WinHttpSetTimeouts(session, 150, 150, 300, 300);

    HINTERNET connect = WinHttpConnect(session, endpoint.host.c_str(), endpoint.port, 0);
    if (!connect) {
        if (errorOut) *errorOut = "WinHttpConnect failed";
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD flags = endpoint.secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", endpoint.path.c_str(),
                                           nullptr, WINHTTP_NO_REFERER,
                                           WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        if (errorOut) *errorOut = "WinHttpOpenRequest failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpSendRequest(request,
                            WINHTTP_NO_ADDITIONAL_HEADERS,
                            0,
                            WINHTTP_NO_REQUEST_DATA,
                            0,
                            0,
                            0)) {
        if (errorOut) *errorOut = "WinHttpSendRequest failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    if (!WinHttpReceiveResponse(request, nullptr)) {
        if (errorOut) *errorOut = "WinHttpReceiveResponse failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    if (!WinHttpQueryHeaders(request,
                             WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX,
                             &statusCode,
                             &statusCodeSize,
                             WINHTTP_NO_HEADER_INDEX)) {
        if (errorOut) *errorOut = "WinHttpQueryHeaders failed";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);

    if (statusCode < 200 || statusCode >= 300) {
        if (errorOut) *errorOut = "HTTP status " + std::to_string(statusCode);
        return false;
    }

    return true;
}
#endif

void TryPostSnapshotImport(const std::string& snapshotPath, bool logEnabled) {
    if (snapshotPath.empty()) return;
    if (ReadEnvToggle("NDEVC_WEB_IMPORT_DISABLE")) return;

    const std::string url = ResolveRuntimeImportUrl();
    if (url.empty()) return;

    std::ostringstream payload;
    payload << "{";
    payload << "\"path\":";
    JsonWriteString(payload, snapshotPath);
    payload << "}";

#if defined(_WIN32)
    std::string err;
    const bool ok = PostJson(url, payload.str(), &err);
    if (!ok && logEnabled) {
        std::cerr << "[WEB IMPORT] POST failed: " << err << " url=" << url << "\n";
    }
#else
    (void)logEnabled;
#endif
}
} // namespace

void DeferredRenderer::WriteWebSnapshot(const char* reason) {
    if (!WebAddonEnabled()) return;

    const std::string snapshotPath = ResolveWebSnapshotPath();
    if (snapshotPath.empty()) {
        return;
    }

    std::error_code ec;
    const std::filesystem::path outPath(snapshotPath);
    if (outPath.has_parent_path()) {
        std::filesystem::create_directories(outPath.parent_path(), ec);
        if (ec) {
            std::cerr << "[WEB SNAPSHOT] Failed to create output directory: " << ec.message() << "\n";
            return;
        }
    }

    std::unordered_map<const Mesh*, SnapshotMeshInfo> meshByPtr;
    auto collectMeshes = [&](const std::vector<DrawCmd>& draws) {
        for (const auto& dc : draws) {
            if (!dc.mesh) continue;
            auto& item = meshByPtr[dc.mesh];
            if (!item.mesh) item.mesh = dc.mesh;
            item.drawRefCount++;
            if (item.meshResourceId.empty() && dc.sourceNode && !dc.sourceNode->mesh_ressource_id.empty()) {
                item.meshResourceId = dc.sourceNode->mesh_ressource_id;
                item.meshPath = BuildMeshPathFromResourceId(item.meshResourceId);
            }
        }
    };

    collectMeshes(solidDraws);
    collectMeshes(alphaTestDraws);
    collectMeshes(simpleLayerDraws);
    collectMeshes(environmentDraws);
    collectMeshes(environmentAlphaDraws);
    collectMeshes(refractionDraws);
    collectMeshes(waterDraws);
    collectMeshes(postAlphaUnlitDraws);
    collectMeshes(decalDraws);
    collectMeshes(particleDraws);

    std::vector<SnapshotMeshInfo> meshes;
    meshes.reserve(meshByPtr.size());
    for (const auto& [_, info] : meshByPtr) {
        meshes.push_back(info);
    }
    std::sort(meshes.begin(), meshes.end(),
              [](const SnapshotMeshInfo& a, const SnapshotMeshInfo& b) {
                  if (a.meshResourceId != b.meshResourceId) return a.meshResourceId < b.meshResourceId;
                  if (a.meshPath != b.meshPath) return a.meshPath < b.meshPath;
                  return a.mesh < b.mesh;
              });

    std::ostringstream os;
    os << "{";
    os << "\"schema\":\"ndevc.runtime.snapshot.v1\",";
    os << "\"generatedUtc\":";
    JsonWriteString(os, UtcNowIso8601());
    os << ",";
    os << "\"reason\":";
    JsonWriteString(os, reason ? std::string(reason) : std::string("unspecified"));
    os << ",";
    os << "\"mapSourcePath\":";
    JsonWriteString(os, currentMapSourcePath_);
    os << ",";

    os << "\"map\":";
    if (!currentMap) {
        os << "null";
    } else {
        os << "{";
        os << "\"size\":["
           << currentMap->info.map_size_x << ","
           << currentMap->info.map_size_y << ","
           << currentMap->info.map_size_z << "],";
        os << "\"center\":";
        JsonWriteVec4(os, currentMap->info.center);
        os << ",";
        os << "\"extents\":";
        JsonWriteVec4(os, currentMap->info.extents);
        os << ",";
        os << "\"gridSize\":";
        JsonWriteVec4(os, currentMap->info.grid_size);
        os << ",";
        os << "\"stringCount\":" << currentMap->string_table.size() << ",";
        os << "\"templateCount\":" << currentMap->templates.size() << ",";
        os << "\"groupCount\":" << currentMap->groups.size() << ",";
        os << "\"instanceCount\":" << currentMap->instances.size() << ",";
        os << "\"instances\":[";
        for (size_t i = 0; i < currentMap->instances.size(); ++i) {
            const auto& inst = currentMap->instances[i];
            if (i > 0) os << ",";
            os << "{";
            os << "\"index\":" << i << ",";
            os << "\"templateIndex\":" << inst.templ_index << ",";
            os << "\"groupIndex\":" << inst.group_index << ",";
            os << "\"eventMappingIndex\":" << inst.index_to_mapping << ",";
            os << "\"useScaling\":" << (inst.use_scaling ? "true" : "false") << ",";
            os << "\"useCollide\":" << (inst.use_collide ? "true" : "false") << ",";
            os << "\"visibleForNavMeshGen\":" << (inst.is_visible_for_nav_mesh_gen ? "true" : "false") << ",";
            os << "\"position\":";
            JsonWriteVec4(os, inst.pos);
            os << ",";
            os << "\"rotation\":";
            JsonWriteVec4(os, inst.rot);
            os << ",";
            os << "\"scale\":";
            JsonWriteVec4(os, inst.use_scaling ? inst.scale : glm::vec4(1, 1, 1, 0));
            if (inst.templ_index >= 0 && static_cast<size_t>(inst.templ_index) < currentMap->templates.size()) {
                const auto& t = currentMap->templates[inst.templ_index];
                os << ",\"templateGfxResId\":" << t.gfx_res_id;
                if (t.gfx_res_id < currentMap->string_table.size()) {
                    os << ",\"templateName\":";
                    JsonWriteString(os, currentMap->string_table[t.gfx_res_id]);
                }
            }
            os << "}";
        }
        os << "]";
        os << "}";
    }
    os << ",";

    os << "\"runtime\":{";
    os << "\"loadedModelInstanceCount\":" << instances.size() << ",";
    os << "\"particleNodeCount\":" << particleNodes.size() << ",";
    os << "\"drawCounts\":{";
    os << "\"solid\":" << solidDraws.size() << ",";
    os << "\"alphaTest\":" << alphaTestDraws.size() << ",";
    os << "\"simpleLayer\":" << simpleLayerDraws.size() << ",";
    os << "\"decal\":" << decalDraws.size() << ",";
    os << "\"water\":" << waterDraws.size() << ",";
    os << "\"refraction\":" << refractionDraws.size() << ",";
    os << "\"environment\":" << environmentDraws.size() << ",";
    os << "\"environmentAlpha\":" << environmentAlphaDraws.size() << ",";
    os << "\"postAlphaUnlit\":" << postAlphaUnlitDraws.size() << ",";
    os << "\"particle\":" << particleDraws.size();
    os << "}";
    os << "},";

    os << "\"meshes\":[";
    for (size_t i = 0; i < meshes.size(); ++i) {
        const auto& m = meshes[i];
        if (i > 0) os << ",";
        os << "{";
        os << "\"meshResourceId\":";
        JsonWriteString(os, m.meshResourceId);
        os << ",";
        os << "\"meshPath\":";
        JsonWriteString(os, m.meshPath);
        os << ",";
        bool fileExists = false;
        uintmax_t fileSize = 0;
        if (!m.meshPath.empty()) {
            fileExists = std::filesystem::exists(m.meshPath, ec);
            if (!ec && fileExists) {
                fileSize = std::filesystem::file_size(m.meshPath, ec);
                if (ec) fileSize = 0;
            } else {
                ec.clear();
            }
        }
        os << "\"meshFileExists\":" << (fileExists ? "true" : "false") << ",";
        os << "\"meshFileSizeBytes\":" << fileSize << ",";
        os << "\"drawRefCount\":" << m.drawRefCount << ",";
        os << "\"runtimeVertexCount\":" << (m.mesh ? m.mesh->verts.size() : 0) << ",";
        os << "\"runtimeIndexCount\":" << (m.mesh ? m.mesh->idx.size() : 0) << ",";
        os << "\"runtimeGroupCount\":" << (m.mesh ? m.mesh->groups.size() : 0) << ",";
        os << "\"runtimeDrawCommandCount\":" << (m.mesh ? m.mesh->draw_commands.size() : 0) << ",";
        os << "\"boundingBoxMin\":";
        JsonWriteVec3(os, m.mesh ? m.mesh->boundingBoxMin : glm::vec3(0.0f));
        os << ",";
        os << "\"boundingBoxMax\":";
        JsonWriteVec3(os, m.mesh ? m.mesh->boundingBoxMax : glm::vec3(0.0f));
        os << "}";
    }
    os << "]";

    os << "}";

    const std::filesystem::path tempPath = outPath.string() + ".tmp";
    {
        std::ofstream out(tempPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::cerr << "[WEB SNAPSHOT] Failed to open temp file for write: " << tempPath.string() << "\n";
            return;
        }
        const std::string payload = os.str();
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
    }

    std::filesystem::rename(tempPath, outPath, ec);
    if (ec) {
        ec.clear();
        std::filesystem::copy_file(tempPath, outPath, std::filesystem::copy_options::overwrite_existing, ec);
        std::filesystem::remove(tempPath, ec);
    }

    TryPostSnapshotImport(outPath.string(), optRenderLOG);
}

void DeferredRenderer::NotifyWebModelLoaded(const std::string& modelPath, const std::string& meshResourceId) {
    if (!WebAddonEnabled()) return;

    if (!ReadEnvToggle("NDEVC_WEB_EVENTS_DISABLE")) {
        std::ostringstream payload;
        payload << "{";
        payload << "\"type\":\"model_loaded\",";
        payload << "\"meshResourceId\":";
        JsonWriteString(payload, meshResourceId);
        payload << ",";
        payload << "\"modelPath\":";
        JsonWriteString(payload, modelPath);
        payload << "}";

#if defined(_WIN32)
        std::string err;
        if (!PostJson(ResolveRuntimeEventsUrl(), payload.str(), &err) && optRenderLOG) {
            std::cerr << "[WEB EVENTS] model_loaded failed: " << err << "\n";
        }
#endif
    }

    if (!ReadEnvToggle("NDEVC_WEB_CDN_GET_DISABLE")) {
        const std::string url = MakeCdnGetUrlForLocalPath(modelPath);
        if (!url.empty()) {
#if defined(_WIN32)
            std::string err;
            if (!GetUrl(url, &err) && optRenderLOG) {
                std::cerr << "[WEB CDN] GET failed: " << err << " url=" << url << "\n";
            }
#endif
        }
    }
}

void DeferredRenderer::NotifyWebModelUnloaded(const std::string& modelPath, const std::string& meshResourceId) {
    if (!WebAddonEnabled()) return;

    if (ReadEnvToggle("NDEVC_WEB_EVENTS_DISABLE")) return;

    std::ostringstream payload;
    payload << "{";
    payload << "\"type\":\"model_unloaded\",";
    payload << "\"meshResourceId\":";
    JsonWriteString(payload, meshResourceId);
    payload << ",";
    payload << "\"modelPath\":";
    JsonWriteString(payload, modelPath);
    payload << "}";

#if defined(_WIN32)
    std::string err;
    if (!PostJson(ResolveRuntimeEventsUrl(), payload.str(), &err) && optRenderLOG) {
        std::cerr << "[WEB EVENTS] model_unloaded failed: " << err << "\n";
    }
#endif
}
