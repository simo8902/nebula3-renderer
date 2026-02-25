using System.Text.Json;
using NDEVC.Web.Bridge.Nvx2;

namespace NDEVC.Web.Api.Runtime;

public sealed class RuntimeSnapshotStore
{
    private readonly Nvx2InfoReader nvx2Reader;
    private readonly JsonSerializerOptions jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true
    };

    private readonly object stateLock = new();
    private RuntimeSnapshotState? current;
    private readonly string defaultSnapshotPath;

    public RuntimeSnapshotStore(Nvx2InfoReader nvx2Reader)
    {
        this.nvx2Reader = nvx2Reader;
        defaultSnapshotPath = ResolveDefaultSnapshotPath();
    }

    public RuntimeSnapshotState? GetCurrent()
    {
        lock (stateLock)
        {
            return current;
        }
    }

    public string GetDefaultSnapshotPath()
    {
        return defaultSnapshotPath;
    }

    public bool TryAutoImport()
    {
        if (GetCurrent() is not null)
        {
            return true;
        }

        var defaultPath = GetDefaultSnapshotPath();
        if (!File.Exists(defaultPath))
        {
            return false;
        }

        var result = Import(defaultPath);
        return result.Success;
    }

    public RuntimeImportResult Import(string? requestedPath)
    {
        var path = ResolveRequestedSnapshotPath(requestedPath);

        if (!File.Exists(path))
        {
            return new RuntimeImportResult
            {
                Success = false,
                Error = $"Snapshot file not found: {path}",
                ResolvedPath = path
            };
        }

        RuntimeSnapshotDocument? snapshot;
        try
        {
            var json = File.ReadAllText(path);
            snapshot = JsonSerializer.Deserialize<RuntimeSnapshotDocument>(json, jsonOptions);
        }
        catch (Exception ex)
        {
            return new RuntimeImportResult
            {
                Success = false,
                Error = $"Failed to read snapshot: {ex.Message}",
                ResolvedPath = path
            };
        }

        if (snapshot is null)
        {
            return new RuntimeImportResult
            {
                Success = false,
                Error = "Snapshot JSON is empty or invalid.",
                ResolvedPath = path
            };
        }

        var snapshotDir = Path.GetDirectoryName(path) ?? Directory.GetCurrentDirectory();
        foreach (var mesh in snapshot.Meshes)
        {
            mesh.Nvx2Info = null;
            mesh.Nvx2Error = null;

            if (string.IsNullOrWhiteSpace(mesh.MeshPath))
            {
                continue;
            }

            var resolvedMeshPath = ResolvePath(mesh.MeshPath, snapshotDir);
            mesh.MeshPath = resolvedMeshPath;

            try
            {
                mesh.Nvx2Info = nvx2Reader.Read(resolvedMeshPath, snapshotDir);
            }
            catch (Exception ex)
            {
                mesh.Nvx2Error = ex.Message;
            }
        }

        var state = new RuntimeSnapshotState
        {
            SourcePath = path,
            ImportedUtc = DateTime.UtcNow,
            Snapshot = snapshot
        };

        lock (stateLock)
        {
            current = state;
        }

        return new RuntimeImportResult
        {
            Success = true,
            State = state,
            ResolvedPath = path
        };
    }

    private static string ResolvePath(string path, string baseDir)
    {
        if (Path.IsPathRooted(path))
        {
            return Path.GetFullPath(path);
        }

        return Path.GetFullPath(Path.Combine(baseDir, path));
    }

    private string ResolveRequestedSnapshotPath(string? requestedPath)
    {
        if (string.IsNullOrWhiteSpace(requestedPath))
        {
            return defaultSnapshotPath;
        }

        if (Path.IsPathRooted(requestedPath))
        {
            return Path.GetFullPath(requestedPath);
        }

        var candidateBases = new List<string>
        {
            Directory.GetCurrentDirectory(),
            Path.GetDirectoryName(defaultSnapshotPath) ?? Directory.GetCurrentDirectory()
        };

        foreach (var start in EnumerateStartDirectories())
        {
            var root = TryFindWorkspaceRoot(start);
            if (!string.IsNullOrWhiteSpace(root))
            {
                candidateBases.Add(root);
                break;
            }
        }

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var fallback = string.Empty;
        foreach (var baseDir in candidateBases)
        {
            var candidate = ResolvePath(requestedPath, baseDir);
            if (!seen.Add(candidate))
            {
                continue;
            }

            if (string.IsNullOrEmpty(fallback))
            {
                fallback = candidate;
            }

            if (File.Exists(candidate))
            {
                return candidate;
            }
        }

        return fallback;
    }

    private static string ResolveDefaultSnapshotPath()
    {
        var env = Environment.GetEnvironmentVariable("NDEVC_WEB_SNAPSHOT_PATH");
        if (!string.IsNullOrWhiteSpace(env))
        {
            return Path.GetFullPath(env);
        }

        foreach (var start in EnumerateStartDirectories())
        {
            var root = TryFindWorkspaceRoot(start);
            if (root is not null)
            {
                return Path.Combine(root, "src", "web", "content", "data", "runtime_snapshot.json");
            }
        }

        return Path.GetFullPath(Path.Combine(
            Directory.GetCurrentDirectory(),
            "src", "web", "content", "data", "runtime_snapshot.json"));
    }

    private static IEnumerable<string> EnumerateStartDirectories()
    {
        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var candidates = new[]
        {
            Directory.GetCurrentDirectory(),
            AppContext.BaseDirectory,
            Environment.ProcessPath is null ? string.Empty : Path.GetDirectoryName(Environment.ProcessPath) ?? string.Empty
        };

        foreach (var candidate in candidates)
        {
            if (string.IsNullOrWhiteSpace(candidate))
            {
                continue;
            }

            var full = Path.GetFullPath(candidate);
            if (seen.Add(full))
            {
                yield return full;
            }
        }
    }

    private static string? TryFindWorkspaceRoot(string startDirectory)
    {
        var currentDir = new DirectoryInfo(startDirectory);
        while (currentDir is not null)
        {
            var marker = Path.Combine(currentDir.FullName, "src", "web", "content");
            if (Directory.Exists(marker))
            {
                return currentDir.FullName;
            }

            currentDir = currentDir.Parent;
        }

        return null;
    }
}
