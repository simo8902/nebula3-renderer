// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MESHSERVER_H
#define NDEVC_MESHSERVER_H

#include "Core/Logger.h"
#include "Rendering/MegaBuffer.h"
#include "Platform/NDEVcHeaders.h"
#include "Rendering/Mesh.h"
#include "Assets/ResourceServer.h"

class MeshServer {
public:
    static MeshServer& instance() {
        static MeshServer srv;
        return srv;
    }

    Mesh* loadMesh(const std::string& meshResId) {
        const auto &it = meshCache.find(meshResId);
        if (it != meshCache.end()) {
            return it->second.get();
        }

        std::string actualPath = meshResId;
        if (actualPath.starts_with("msh:")) {
            actualPath = actualPath.substr(4); // remove "msh:"
        }

        actualPath = MESHES_ROOT + actualPath;

        auto mesh = std::make_unique<Mesh>();
        if (!Mesh::LoadNVX2(actualPath, *mesh)) {
            NC::LOGGING::Error("[MESH] load failed path=", actualPath, " id=", meshResId);
            return nullptr;
        }

        Mesh* ptr = mesh.get();
        meshCache[meshResId] = std::move(mesh);
        return ptr;
    }

    void buildMegaBuffer() {
        printf("[MEGABUFFER_BUILD] Starting with meshCount=%zu\n", meshCache.size());
        fflush(stdout);

        std::vector<ObjVertex> allVerts;
        std::vector<uint32_t>  allIndices;

        size_t totalV = 0, totalI = 0;
        for (auto& [id, mesh] : meshCache) {
            totalV += mesh->verts.size();
            totalI += mesh->idx.size();
        }
        allVerts.reserve(totalV);
        allIndices.reserve(totalI);

        int meshIdx = 0;
        for (auto& [id, mesh] : meshCache) {
            mesh->megaVertexOffset = (uint32_t)allVerts.size();
            mesh->megaIndexOffset  = (uint32_t)allIndices.size();

            printf("[MEGABUFFER_MESH %d] id=%s vertCount=%zu indexCount=%zu megaVertexOffset=%u megaIndexOffset=%u\n",
                   meshIdx++, id.c_str(), mesh->verts.size(), mesh->idx.size(),
                   mesh->megaVertexOffset, mesh->megaIndexOffset);
            fflush(stdout);

             for (uint32_t idx : mesh->idx)
                allIndices.push_back(idx + mesh->megaVertexOffset);


            allVerts.insert(allVerts.end(), mesh->verts.begin(), mesh->verts.end());
        }

        MegaBuffer::instance().build(allVerts, allIndices);
        printf("[MEGABUFFER_BUILD] Finished: totalVerts=%zu totalIndices=%zu\n", allVerts.size(), allIndices.size());
        fflush(stdout);

        ResourceServer::instance().Clear();
        for (auto& [id, mesh] : meshCache) {
            ResourceServer::instance().Register(mesh.get());
        }
    }

    void clearCache() {
        NC::LOGGING::Log("[MESH] clearCache size=", meshCache.size());
        meshCache.clear();
    }

    std::vector<std::string> GetLoadedMeshIds() const {
        std::vector<std::string> out;
        out.reserve(meshCache.size());
        for (const auto& [id, mesh] : meshCache) {
            (void)mesh;
            out.push_back(id);
        }
        return out;
    }

private:
    MeshServer() = default;
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshCache;
};

#endif //NDEVC_MESHSERVER_H
