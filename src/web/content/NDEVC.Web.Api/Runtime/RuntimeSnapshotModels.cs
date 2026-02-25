using NDEVC.Web.Bridge.Nvx2;

namespace NDEVC.Web.Api.Runtime;

public sealed class RuntimeImportRequest
{
    public string? Path { get; set; }
}

public sealed class RuntimeImportResult
{
    public bool Success { get; set; }
    public string? Error { get; set; }
    public string? ResolvedPath { get; set; }
    public RuntimeSnapshotState? State { get; set; }
}

public sealed class RuntimeSnapshotState
{
    public string SourcePath { get; set; } = string.Empty;
    public DateTime ImportedUtc { get; set; }
    public RuntimeSnapshotDocument Snapshot { get; set; } = new();
}

public sealed class RuntimeSnapshotDocument
{
    public string Schema { get; set; } = string.Empty;
    public string GeneratedUtc { get; set; } = string.Empty;
    public string Reason { get; set; } = string.Empty;
    public string MapSourcePath { get; set; } = string.Empty;
    public RuntimeMapData? Map { get; set; }
    public RuntimeLoadedData? Runtime { get; set; }
    public List<RuntimeMeshData> Meshes { get; set; } = [];
}

public sealed class RuntimeMapData
{
    public List<float> Size { get; set; } = [];
    public List<float> Center { get; set; } = [];
    public List<float> Extents { get; set; } = [];
    public List<float> GridSize { get; set; } = [];
    public int StringCount { get; set; }
    public int TemplateCount { get; set; }
    public int GroupCount { get; set; }
    public int InstanceCount { get; set; }
    public List<RuntimeMapInstanceData> Instances { get; set; } = [];
}

public sealed class RuntimeMapInstanceData
{
    public int Index { get; set; }
    public int TemplateIndex { get; set; }
    public int GroupIndex { get; set; }
    public int EventMappingIndex { get; set; }
    public bool UseScaling { get; set; }
    public bool UseCollide { get; set; }
    public bool VisibleForNavMeshGen { get; set; }
    public List<float> Position { get; set; } = [];
    public List<float> Rotation { get; set; } = [];
    public List<float> Scale { get; set; } = [];
    public int? TemplateGfxResId { get; set; }
    public string? TemplateName { get; set; }
}

public sealed class RuntimeLoadedData
{
    public int LoadedModelInstanceCount { get; set; }
    public int ParticleNodeCount { get; set; }
    public RuntimeDrawCounts DrawCounts { get; set; } = new();
}

public sealed class RuntimeDrawCounts
{
    public int Solid { get; set; }
    public int AlphaTest { get; set; }
    public int SimpleLayer { get; set; }
    public int Decal { get; set; }
    public int Water { get; set; }
    public int Refraction { get; set; }
    public int Environment { get; set; }
    public int EnvironmentAlpha { get; set; }
    public int PostAlphaUnlit { get; set; }
    public int Particle { get; set; }
}

public sealed class RuntimeMeshData
{
    public string MeshResourceId { get; set; } = string.Empty;
    public string MeshPath { get; set; } = string.Empty;
    public bool MeshFileExists { get; set; }
    public long MeshFileSizeBytes { get; set; }
    public long DrawRefCount { get; set; }
    public long RuntimeVertexCount { get; set; }
    public long RuntimeIndexCount { get; set; }
    public long RuntimeGroupCount { get; set; }
    public long RuntimeDrawCommandCount { get; set; }
    public List<float> BoundingBoxMin { get; set; } = [];
    public List<float> BoundingBoxMax { get; set; } = [];

    public Nvx2MeshInfo? Nvx2Info { get; set; }
    public string? Nvx2Error { get; set; }
}
