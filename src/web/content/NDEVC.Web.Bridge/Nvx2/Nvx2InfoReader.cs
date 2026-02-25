using System.Text;

namespace NDEVC.Web.Bridge.Nvx2;

public sealed class Nvx2InfoReader
{
    private const int HeaderSizeBytes = 24;
    private const int GroupSizeBytes = 24;

    public Nvx2MeshInfo Read(string path, string? baseDirectory = null)
    {
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Path must be non-empty.", nameof(path));
        }

        var resolvedPath = ResolvePath(path, baseDirectory);
        if (!File.Exists(resolvedPath))
        {
            throw new FileNotFoundException("NVX2 file does not exist.", resolvedPath);
        }

        using var stream = new FileStream(resolvedPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        using var reader = new BinaryReader(stream, Encoding.ASCII, leaveOpen: false);
        if (stream.Length < HeaderSizeBytes)
        {
            throw new InvalidDataException($"File is too small to be a valid NVX2. Length={stream.Length} bytes.");
        }

        var info = new Nvx2MeshInfo
        {
            RequestedPath = path,
            ResolvedPath = resolvedPath,
            FileSizeBytes = stream.Length,
            HeaderBytes = HeaderSizeBytes
        };

        var magicBytes = reader.ReadBytes(4);
        info.MagicHex = Convert.ToHexString(magicBytes);
        info.MagicAscii = ToPrintableAscii(magicBytes);

        info.NumGroups = reader.ReadUInt32();
        info.NumVertices = reader.ReadUInt32();
        info.VertexWidthFloats = reader.ReadUInt32();
        info.NumTrianglesOrIndices = reader.ReadUInt32();
        info.NumEdges = reader.ReadUInt32();
        info.ComponentMask = reader.ReadUInt32();

        info.DeclaredVertexStrideBytes = checked((int)info.VertexWidthFloats * sizeof(float));
        PopulateComponentLayout(info);

        info.GroupTableBytes = checked((long)info.NumGroups * GroupSizeBytes);
        var groupTableEndOffset = HeaderSizeBytes + info.GroupTableBytes;
        if (stream.Length < groupTableEndOffset)
        {
            throw new InvalidDataException(
                $"File ended before group table. Needed offset {groupTableEndOffset}, file length {stream.Length}.");
        }

        for (uint i = 0; i < info.NumGroups; i++)
        {
            var group = new Nvx2GroupInfo
            {
                GroupIndex = i,
                FirstVertex = reader.ReadUInt32(),
                NumVertices = reader.ReadUInt32(),
                FirstTriangle = reader.ReadUInt32(),
                NumTriangles = reader.ReadUInt32(),
                FirstEdge = reader.ReadUInt32(),
                NumEdges = reader.ReadUInt32()
            };
            group.FirstIndex = group.FirstTriangle * 3;
            group.IndexCount = group.NumTriangles * 3;
            info.Groups.Add(group);
        }

        info.VertexBufferOffset = HeaderSizeBytes + info.GroupTableBytes;
        info.VertexBufferBytes = checked((long)info.NumVertices * info.DeclaredVertexStrideBytes);
        info.IndexBufferOffset = info.VertexBufferOffset + info.VertexBufferBytes;

        if (info.IndexBufferOffset > info.FileSizeBytes)
        {
            info.Warnings.Add(
                $"Declared vertex buffer exceeds file size. Vertex end offset {info.IndexBufferOffset}, file size {info.FileSizeBytes}.");
            info.RemainingBytesAfterDeclaredDataOffset = 0;
        }
        else
        {
            info.RemainingBytesAfterDeclaredDataOffset = info.FileSizeBytes - info.IndexBufferOffset;
        }

        uint totalTriangles = 0;
        foreach (var group in info.Groups)
        {
            totalTriangles += group.NumTriangles;
        }

        info.GroupTriangleCountSum = totalTriangles;
        info.ExpectedIndexCount = totalTriangles > 0 ? totalTriangles * 3u : info.NumTrianglesOrIndices;

        var need16 = checked((long)info.ExpectedIndexCount * sizeof(ushort));
        var need32 = checked((long)info.ExpectedIndexCount * sizeof(uint));
        var remaining = info.RemainingBytesAfterDeclaredDataOffset;

        info.IsLikely32BitIndices =
            (remaining >= need32 && remaining % 4 == 0 && info.NumVertices >= 65536) ||
            remaining == need32;

        info.InferredIndexElementSizeBytes = info.IsLikely32BitIndices ? sizeof(uint) : sizeof(ushort);
        info.InferredIndexBufferBytes = checked((long)info.ExpectedIndexCount * info.InferredIndexElementSizeBytes);
        info.IndexBufferFitsInFile = remaining >= info.InferredIndexBufferBytes;

        if (!info.IndexBufferFitsInFile)
        {
            info.Warnings.Add(
                $"Index buffer appears truncated. Need {info.InferredIndexBufferBytes} bytes, remaining {remaining} bytes.");
        }
        else if (remaining > info.InferredIndexBufferBytes)
        {
            info.Warnings.Add(
                $"File has trailing bytes after inferred index buffer: {remaining - info.InferredIndexBufferBytes} bytes.");
        }

        return info;
    }

    private static string ResolvePath(string requestedPath, string? baseDirectory)
    {
        if (Path.IsPathRooted(requestedPath))
        {
            return Path.GetFullPath(requestedPath);
        }

        var basePath = string.IsNullOrWhiteSpace(baseDirectory)
            ? Directory.GetCurrentDirectory()
            : baseDirectory;
        return Path.GetFullPath(Path.Combine(basePath, requestedPath));
    }

    private static string ToPrintableAscii(byte[] bytes)
    {
        var chars = new char[bytes.Length];
        for (var i = 0; i < bytes.Length; i++)
        {
            chars[i] = bytes[i] is >= 32 and <= 126 ? (char)bytes[i] : '.';
        }
        return new string(chars);
    }

    private static void PopulateComponentLayout(Nvx2MeshInfo info)
    {
        var knownMask = 0u;
        var offset = 0;

        foreach (var def in Nvx2ComponentCatalog.Ordered)
        {
            var bit = (uint)def.Bit;
            knownMask |= bit;
            if ((info.ComponentMask & bit) == 0)
            {
                continue;
            }

            info.Components.Add(new Nvx2ComponentLayoutEntry
            {
                Name = def.Name,
                Bit = bit,
                OffsetBytes = offset,
                SizeBytes = def.SizeBytes
            });
            offset += def.SizeBytes;
        }

        info.CalculatedVertexStrideBytes = offset;
        if (info.CalculatedVertexStrideBytes != info.DeclaredVertexStrideBytes)
        {
            info.Warnings.Add(
                $"Declared vertex stride ({info.DeclaredVertexStrideBytes}) differs from component mask layout ({info.CalculatedVertexStrideBytes}).");
        }

        var unknownMask = info.ComponentMask & ~knownMask;
        if (unknownMask != 0)
        {
            info.Warnings.Add($"Component mask has unknown bits set: 0x{unknownMask:X8}.");
        }
    }
}
