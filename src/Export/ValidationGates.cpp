// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#include "Export/ValidationGates.h"
#include "Core/Logger.h"

#include <algorithm>
#include <cstdio>

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// EstimateFrameCost
// Each solid draw ≈ 0.003 ms GPU, each alpha draw ≈ 0.005 ms, base ≈ 2 ms
// ---------------------------------------------------------------------------
float ValidationGates::EstimateFrameCost(int solidCount, int alphaCount)
{
    return 2.0f + solidCount * 0.003f + alphaCount * 0.005f;
}

// ---------------------------------------------------------------------------
// SortTopOffenders — keep top N by VRAM cost
// ---------------------------------------------------------------------------
void ValidationGates::SortTopOffenders(ExportContext& ctx, int maxEntries)
{
    auto& v = ctx.report.topOffenders;
    std::sort(v.begin(), v.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (static_cast<int>(v.size()) > maxEntries)
        v.resize(static_cast<size_t>(maxEntries));
}

// ---------------------------------------------------------------------------
// CheckVRAMBudget
// ---------------------------------------------------------------------------
void ValidationGates::CheckVRAMBudget(ExportContext& ctx)
{
    const uint64_t limit = ctx.budgets.maxVRAMBytes;
    if (limit == 0) return; // unlimited

    const uint64_t used = ctx.report.totalVRAMEstimateBytes;
    if (used <= limit) return;

    const double usedMB  = static_cast<double>(used)  / (1024.0 * 1024.0);
    const double limitMB = static_cast<double>(limit) / (1024.0 * 1024.0);
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "VRAM estimate %.1f MB exceeds budget %.1f MB", usedMB, limitMB);

    if (ctx.profile == ExportProfile::Shipping) {
        ctx.AddError("scene", msg);
        NC::LOGGING::Error("[VALIDATE] ", msg);
    } else {
        ctx.AddWarning("scene", msg);
        NC::LOGGING::Warning("[VALIDATE] ", msg);
    }
}

// ---------------------------------------------------------------------------
// CheckDrawCallBudget
// ---------------------------------------------------------------------------
void ValidationGates::CheckDrawCallBudget(ExportContext& ctx)
{
    const int limit = ctx.budgets.maxDrawCalls;
    if (limit == 0) return;

    const int used = ctx.report.totalDrawCalls;
    if (used <= limit) return;

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "Draw call count %d exceeds budget %d", used, limit);

    if (ctx.profile == ExportProfile::Shipping) {
        ctx.AddError("scene", msg);
        NC::LOGGING::Error("[VALIDATE] ", msg);
    } else {
        ctx.AddWarning("scene", msg);
        NC::LOGGING::Warning("[VALIDATE] ", msg);
    }
}

// ---------------------------------------------------------------------------
// CheckFrameCostBudget
// ---------------------------------------------------------------------------
void ValidationGates::CheckFrameCostBudget(ExportContext& ctx)
{
    const float limit = ctx.budgets.maxFrameCostEstimate;
    if (limit <= 0.0f) return;

    const int solidCount = static_cast<int>(ctx.solidDraws.size());
    const int alphaCount = static_cast<int>(ctx.alphaDraws.size());
    const float estimate = EstimateFrameCost(solidCount, alphaCount);
    ctx.report.frameCostEstimate = estimate;

    if (estimate <= limit) return;

    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "Frame cost estimate %.2f ms exceeds budget %.2f ms", estimate, limit);

    if (ctx.profile == ExportProfile::Shipping) {
        ctx.AddError("scene", msg);
        NC::LOGGING::Error("[VALIDATE] ", msg);
    } else {
        ctx.AddWarning("scene", msg);
        NC::LOGGING::Warning("[VALIDATE] ", msg);
    }
}

// ---------------------------------------------------------------------------
// CheckPackageSizeBudget
// ---------------------------------------------------------------------------
void ValidationGates::CheckPackageSizeBudget(ExportContext& ctx)
{
    const uint64_t limit = ctx.budgets.maxPackageSizeBytes;
    if (limit == 0) return;

    const uint64_t used = ctx.report.totalPackageSizeBytes;
    if (used == 0 || used <= limit) return; // 0 = not yet known, skip

    const double usedMB  = static_cast<double>(used)  / (1024.0 * 1024.0);
    const double limitMB = static_cast<double>(limit) / (1024.0 * 1024.0);
    char msg[256];
    std::snprintf(msg, sizeof(msg),
                  "Package size %.1f MB exceeds budget %.1f MB", usedMB, limitMB);

    if (ctx.profile == ExportProfile::Shipping) {
        ctx.AddError("scene", msg);
        NC::LOGGING::Error("[VALIDATE] ", msg);
    } else {
        ctx.AddWarning("scene", msg);
        NC::LOGGING::Warning("[VALIDATE] ", msg);
    }
}

// ---------------------------------------------------------------------------
// Validate — run all gates and report top offenders
// ---------------------------------------------------------------------------
bool ValidationGates::Validate(ExportContext& ctx)
{
    // Compute frame cost estimate if not done yet
    if (ctx.report.frameCostEstimate <= 0.0f) {
        ctx.report.frameCostEstimate = EstimateFrameCost(
            static_cast<int>(ctx.solidDraws.size()),
            static_cast<int>(ctx.alphaDraws.size()));
    }

    CheckVRAMBudget(ctx);
    CheckDrawCallBudget(ctx);
    CheckFrameCostBudget(ctx);
    CheckPackageSizeBudget(ctx);

    SortTopOffenders(ctx);

    // Summary
    const int errorCount   = static_cast<int>(std::count_if(
        ctx.report.issues.begin(), ctx.report.issues.end(),
        [](const ExportIssue& i) { return i.severity == ExportIssue::Severity::Error; }));
    const int warningCount = static_cast<int>(ctx.report.issues.size()) - errorCount;

    NC::LOGGING::Log("[VALIDATE] Complete: errors=", errorCount,
                     " warnings=", warningCount,
                     " vram=",
                     static_cast<double>(ctx.report.totalVRAMEstimateBytes) / (1024.0 * 1024.0),
                     "MB draws=", ctx.report.totalDrawCalls,
                     " frameCostEst=", ctx.report.frameCostEstimate, "ms");

    if (!ctx.report.topOffenders.empty()) {
        NC::LOGGING::Log("[VALIDATE] Top VRAM offenders:");
        for (const auto& [name, vram] : ctx.report.topOffenders) {
            NC::LOGGING::Log("  ", name, " = ",
                             static_cast<double>(vram) / (1024.0 * 1024.0), " MB");
        }
    }

    // Return false only for Shipping with hard errors
    return ctx.report.success;
}

} // namespace NDEVC::Export
