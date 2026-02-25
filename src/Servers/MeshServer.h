// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MESHSERVER_H
#define NDEVC_MESHSERVER_H

#include "MegaBuffer.h"
#include "../NDEVcHeaders.h"
#include "Mesh.h"

class MeshServer {
public:
    static MeshServer& instance() {
        static MeshServer srv;
        return srv;
    }

    Mesh* loadMesh(const std::string& meshResId) {
        const auto &it = meshCache.find(meshResId);
        if (it != meshCache.end()) return it->second.get();

        std::string actualPath = meshResId;
        if (actualPath.starts_with("msh:")) {
            actualPath = actualPath.substr(4); // remove "msh:"
        }

        actualPath = MESHES_ROOT + actualPath;

        auto mesh = std::make_unique<Mesh>();
        if (!Mesh::LoadNVX2(actualPath, *mesh)) {
            std::cerr << "[MESH][ERROR] Failed to load '" << actualPath << "' (id='" << meshResId << "')\n";
            return nullptr;
        }

        Mesh* ptr = mesh.get();
        meshCache[meshResId] = std::move(mesh);
        return ptr;
    }

    void buildMegaBuffer() {
        std::vector<ObjVertex> allVerts;
        std::vector<uint32_t>  allIndices;

        size_t totalV = 0, totalI = 0;
        for (auto& [id, mesh] : meshCache) {
            totalV += mesh->verts.size();
            totalI += mesh->idx.size();
        }
        allVerts.reserve(totalV);
        allIndices.reserve(totalI);

        for (auto& [id, mesh] : meshCache) {
            mesh->megaVertexOffset = (uint32_t)allVerts.size();
            mesh->megaIndexOffset  = (uint32_t)allIndices.size();

             for (uint32_t idx : mesh->idx)
                allIndices.push_back(idx + mesh->megaVertexOffset);


            allVerts.insert(allVerts.end(), mesh->verts.begin(), mesh->verts.end());
        }

        MegaBuffer::instance().build(allVerts, allIndices);
    }

    void clearCache() { meshCache.clear(); }

private:
    MeshServer() = default;
    std::unordered_map<std::string, std::unique_ptr<Mesh>> meshCache;
};

#endif //NDEVC_MESHSERVER_H
