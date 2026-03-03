// Copyright (c) 2026 Simeon Mladenov and DSO Reconstruction Team. All rights reserved.
// Unauthorized copying, modification, distribution, or use is strictly prohibited.

#ifndef NDEVC_VALIDATION_GATES_H
#define NDEVC_VALIDATION_GATES_H

#include "Export/ExportTypes.h"

namespace NDEVC::Export {

// ---------------------------------------------------------------------------
// ValidationGates — budget checks and top-offender reporting
//
// Errors are emitted for Shipping profile when a budget is exceeded.
// Warnings are emitted for Work and Playtest profiles.
// Non-zero limits of zero mean "unlimited" (no gate applied).
// ---------------------------------------------------------------------------
class ValidationGates {
public:
    // Run all budget gates. Results pushed into ctx.report.
    // Returns true unless a hard error occurred (Shipping + budget exceeded).
    static bool Validate(ExportContext& ctx);

private:
    // Individual gate checkers
    static void CheckVRAMBudget(ExportContext& ctx);
    static void CheckDrawCallBudget(ExportContext& ctx);
    static void CheckFrameCostBudget(ExportContext& ctx);
    static void CheckPackageSizeBudget(ExportContext& ctx);

    // Sort topOffenders descending by VRAM and truncate to top N
    static void SortTopOffenders(ExportContext& ctx, int maxEntries = 10);

    // Compute frame cost estimate from draw counts
    static float EstimateFrameCost(int solidCount, int alphaCount);
};

} // namespace NDEVC::Export

#endif // NDEVC_VALIDATION_GATES_H
