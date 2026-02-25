// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Model.h"
#include "../Parser.h"
#include "../Mesh.h"

bool Model::loadFromFile(const std::string& filepath, Reporter& rep, const Options& opt) {
    Parser parser(rep, opt);
    if (!parser.parse_file(filepath)) return false;

    rootNode = parser.getRootNode();
    nodeList = parser.getNodeList();
    nodeStorage = std::move(parser.getNodeStorage());
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
