// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_MODEL_H
#define NDEVC_MODEL_H

#include <string>
#include <vector>

struct Node;
struct Reporter;
struct Options;
class Parser;
class Mesh;

class Model {
public:
    Model() = default;
    ~Model() = default;

    bool loadFromFile(const std::string& filepath, Reporter& rep, const Options& opt);

    Node* getRootNode() const { return rootNode; }
    const std::vector<Node*>& getNodes() const { return nodeList; }
    const std::string& getModelType() const { return modelType; }
    const std::string& getModelName() const { return modelName; }
    int getVersion() const { return version; }

    std::vector<Mesh*> getMeshes() const;

private:
    Node* rootNode = nullptr;
    std::vector<Node*> nodeList;
    std::vector<Node> nodeStorage;

    std::string modelType;
    std::string modelName;
    int version = 0;

    friend class Parser;
};

#endif //NDEVC_MODEL_H