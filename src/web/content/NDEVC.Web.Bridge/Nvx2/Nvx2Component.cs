using System.Collections.ObjectModel;

namespace NDEVC.Web.Bridge.Nvx2;

[Flags]
public enum Nvx2Component : uint
{
    Coord = 1u << 0,
    Normal = 1u << 1,
    NormalUb4n = 1u << 2,
    Uv0 = 1u << 3,
    Uv0S2 = 1u << 4,
    Uv1 = 1u << 5,
    Uv1S2 = 1u << 6,
    Uv2 = 1u << 7,
    Uv2S2 = 1u << 8,
    Uv3 = 1u << 9,
    Uv3S2 = 1u << 10,
    Color = 1u << 11,
    ColorUb4n = 1u << 12,
    Tangent = 1u << 13,
    TangentUb4n = 1u << 14,
    Binormal = 1u << 15,
    BinormalUb4n = 1u << 16,
    Weights = 1u << 17,
    WeightsUb4n = 1u << 18,
    JIndices = 1u << 19,
    JIndicesUb4 = 1u << 20
}

public static class Nvx2ComponentCatalog
{
    public static readonly ReadOnlyCollection<Nvx2ComponentDefinition> Ordered = new(
    [
        new("Coord", Nvx2Component.Coord, 12),
        new("Normal", Nvx2Component.Normal, 12),
        new("NormalUB4N", Nvx2Component.NormalUb4n, 4),
        new("Uv0", Nvx2Component.Uv0, 8),
        new("Uv0S2", Nvx2Component.Uv0S2, 4),
        new("Uv1", Nvx2Component.Uv1, 8),
        new("Uv1S2", Nvx2Component.Uv1S2, 4),
        new("Uv2", Nvx2Component.Uv2, 8),
        new("Uv2S2", Nvx2Component.Uv2S2, 4),
        new("Uv3", Nvx2Component.Uv3, 8),
        new("Uv3S2", Nvx2Component.Uv3S2, 4),
        new("Color", Nvx2Component.Color, 16),
        new("ColorUB4N", Nvx2Component.ColorUb4n, 4),
        new("Tangent", Nvx2Component.Tangent, 12),
        new("TangentUB4N", Nvx2Component.TangentUb4n, 4),
        new("Binormal", Nvx2Component.Binormal, 12),
        new("BinormalUB4N", Nvx2Component.BinormalUb4n, 4),
        new("Weights", Nvx2Component.Weights, 16),
        new("WeightsUB4N", Nvx2Component.WeightsUb4n, 4),
        new("JIndices", Nvx2Component.JIndices, 16),
        new("JIndicesUB4", Nvx2Component.JIndicesUb4, 4)
    ]);

    public static IReadOnlyList<Nvx2ComponentDefinition> GetAll() => Ordered;
}

public sealed record Nvx2ComponentDefinition(string Name, Nvx2Component Bit, int SizeBytes);
