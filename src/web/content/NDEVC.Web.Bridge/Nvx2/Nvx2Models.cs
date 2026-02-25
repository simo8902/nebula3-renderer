namespace NDEVC.Web.Bridge.Nvx2;

public sealed class Nvx2GroupInfo
{
    public uint GroupIndex { get; set; }
    public uint FirstVertex { get; set; }
    public uint NumVertices { get; set; }
    public uint FirstTriangle { get; set; }
    public uint NumTriangles { get; set; }
    public uint FirstEdge { get; set; }
    public uint NumEdges { get; set; }
    public uint FirstIndex { get; set; }
    public uint IndexCount { get; set; }
}

public sealed class Nvx2ComponentLayoutEntry
{
    public string Name { get; set; } = string.Empty;
    public uint Bit { get; set; }
    public int OffsetBytes { get; set; }
    public int SizeBytes { get; set; }
}

public sealed class Nvx2MeshInfo
{
    public string RequestedPath { get; set; } = string.Empty;
    public string ResolvedPath { get; set; } = string.Empty;
    public long FileSizeBytes { get; set; }

    public string MagicAscii { get; set; } = string.Empty;
    public string MagicHex { get; set; } = string.Empty;

    public uint NumGroups { get; set; }
    public uint NumVertices { get; set; }
    public uint VertexWidthFloats { get; set; }
    public uint NumTrianglesOrIndices { get; set; }
    public uint NumEdges { get; set; }
    public uint ComponentMask { get; set; }

    public int DeclaredVertexStrideBytes { get; set; }
    public int CalculatedVertexStrideBytes { get; set; }

    public long HeaderBytes { get; set; }
    public long GroupTableBytes { get; set; }
    public long VertexBufferOffset { get; set; }
    public long VertexBufferBytes { get; set; }
    public long IndexBufferOffset { get; set; }
    public long RemainingBytesAfterDeclaredDataOffset { get; set; }

    public uint GroupTriangleCountSum { get; set; }
    public uint ExpectedIndexCount { get; set; }
    public bool IsLikely32BitIndices { get; set; }
    public int InferredIndexElementSizeBytes { get; set; }
    public long InferredIndexBufferBytes { get; set; }
    public bool IndexBufferFitsInFile { get; set; }

    public List<Nvx2ComponentLayoutEntry> Components { get; } = [];
    public List<Nvx2GroupInfo> Groups { get; } = [];
    public List<string> Warnings { get; } = [];
}
