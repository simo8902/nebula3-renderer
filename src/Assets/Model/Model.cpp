// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Model.h"
#include "Assets/Parser.h"
#include "Rendering/Mesh.h"

bool Model::loadFromFile(const std::string& filepath, Reporter& rep, const Options& opt) {
    Parser parser(rep, opt);
    if (!parser.parse_file(filepath)) return false;

    nodeStorage = parser.takeNodeStorage();
    nodeList.reserve(nodeStorage.size());
    for (Node& n : nodeStorage) nodeList.push_back(&n);
    rootNode = nodeList.empty() ? nullptr : nodeList[0];
    modelType = parser.getModelType();
    modelName = parser.getModelName();
    version = parser.getVersion();

    return true;
}

std::vector<Mesh*> Model::getMeshes() const {
    std::vector<Mesh*> meshes;
    for (auto* node : nodeList) {
        if (node && !node->mesh_ressource_id.empty()) {
            // TODO: load mesh from node->mesh_ressource_id
        }
    }
    return meshes;
}
