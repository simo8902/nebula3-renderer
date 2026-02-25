// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_PARSER_H
#define NDEVC_PARSER_H

#include "NDEVcBinaryReader.h"
#include "NDEVcHeaders.h"
#include "NDEVcStructure.h"

class Parser {
public:
    Parser(Reporter& r, const Options& opt) :rep(r), options(opt) {}

    bool parse_file(const std::string& filepath);

    Node* getRootNode() const { return n3node_list.empty() ? nullptr : n3node_list[0]; }
    std::vector<Node*> getNodeList() const { return n3node_list; }
    std::vector<std::unique_ptr<Node>> getNodeStorage() { return std::move(n3node_storage); }
    std::string getModelType() const { return n3modeltype; }
    std::string getModelName() const { return n3modelname; }
    int getVersion() const { return n3version; }
private:
    Reporter& rep;
    Options options;

    std::string filepath;
    int n3version = 0;
    std::string n3modeltype;
    std::string n3modelname;
    std::vector<Node*> n3node_list;
    std::vector<std::unique_ptr<Node>> n3node_storage;
    std::vector<Node*> nodeStackObj;
    std::vector<std::string> nodeStack;
    std::string currentNFRT;
    std::string lastNFRT;

    bool read_header(NDEVcBinaryReader& reader);
    bool parse_node_tag(NDEVcBinaryReader& reader, const std::string& tag, Node& node, const std::string& ind) const;

    static inline bool is_valid_fourcc(const std::string& tag);
    static const std::unordered_set<std::string> kAllowed;
    bool particleParams = false;
    bool coreParams = false;
};

#endif //NDEVC_PARSER_H