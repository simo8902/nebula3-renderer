// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_EXPORT_GRAPH_H
#define NDEVC_EXPORT_GRAPH_H

#include "Export/ExportCache.h"
#include "Export/ExportTypes.h"
#include <memory>
#include <string>
#include <vector>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// IExportNode — base interface for all pipeline nodes
// ---------------------------------------------------------------------------
class IExportNode {
public:
    virtual ~IExportNode() = default;

    virtual const char*              GetName()       const = 0;
    // Keys this node reads from the context (used for dependency ordering)
    virtual std::vector<std::string> GetInputKeys()  const = 0;
    // Keys this node writes to the context
    virtual std::vector<std::string> GetOutputKeys() const = 0;
    // Returns false on unrecoverable error; warnings are pushed into ctx.report
    virtual bool Execute(ExportContext& ctx)                = 0;
};

// ---------------------------------------------------------------------------
// ExportGraph — DAG pipeline with topological execution
// ---------------------------------------------------------------------------
class ExportGraph {
public:
    void AddNode(std::unique_ptr<IExportNode> node);

    // Execute nodes in dependency order. Stops on first fatal failure.
    // Returns false if any node returned false.
    bool Execute(ExportContext& ctx);

    // Print node names and dependency edges to the log
    void LogTopology() const;

private:
    std::vector<std::unique_ptr<IExportNode>> nodes_;
    // Kahn's topological sort on output→input key edges
    std::vector<IExportNode*> TopoSort() const;
};

// ---------------------------------------------------------------------------
// Built-in pipeline nodes
// ---------------------------------------------------------------------------

// ImportNode: validates sourceScene and populates mesh/texture/material desc lists
class ImportNode : public IExportNode {
public:
    const char* GetName() const override { return "Import"; }
    std::vector<std::string> GetInputKeys() const override { return {}; }
    std::vector<std::string> GetOutputKeys() const override {
        return { "meshDescs", "textureDescs", "materialDescs" };
    }
    bool Execute(ExportContext& ctx) override;
};

// CookNode: applies profile settings, builds draw proxies and visibility cells
class CookNode : public IExportNode {
public:
    const char* GetName() const override { return "Cook"; }
    std::vector<std::string> GetInputKeys() const override {
        return { "meshDescs", "textureDescs", "materialDescs" };
    }
    std::vector<std::string> GetOutputKeys() const override {
        return { "solidDraws", "alphaDraws", "visCells" };
    }
    bool Execute(ExportContext& ctx) override;
};

// OptimizeNode: PVS bake, HLOD generation, shader variant pruning
class OptimizeNode : public IExportNode {
public:
    const char* GetName() const override { return "Optimize"; }
    std::vector<std::string> GetInputKeys() const override {
        return { "solidDraws", "alphaDraws", "visCells" };
    }
    std::vector<std::string> GetOutputKeys() const override {
        return { "pvsData", "hlodData", "prunedVariants" };
    }
    bool Execute(ExportContext& ctx) override;
};

// PackageNode: writes NDPK archive and manifest
class PackageNode : public IExportNode {
public:
    const char* GetName() const override { return "Package"; }
    std::vector<std::string> GetInputKeys() const override {
        return { "pvsData", "hlodData", "prunedVariants" };
    }
    std::vector<std::string> GetOutputKeys() const override {
        return { "outputPackagePath", "manifest" };
    }
    bool Execute(ExportContext& ctx) override;
};

// ValidateNode: checks budgets and produces top-offender report
class ValidateNode : public IExportNode {
public:
    const char* GetName() const override { return "Validate"; }
    std::vector<std::string> GetInputKeys() const override {
        return { "outputPackagePath", "manifest" };
    }
    std::vector<std::string> GetOutputKeys() const override { return {}; }
    bool Execute(ExportContext& ctx) override;
};

// ---------------------------------------------------------------------------
// Factory: build a complete Work/Playtest/Shipping pipeline
// ---------------------------------------------------------------------------
ExportGraph BuildStandardGraph();

} // namespace NDEVC::Export

#endif // NDEVC_EXPORT_GRAPH_H
