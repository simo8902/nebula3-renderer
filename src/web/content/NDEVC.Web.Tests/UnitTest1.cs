using NDEVC.Web.Bridge.Nvx2;

namespace NDEVC.Web.Tests;

public class Nvx2InfoReaderTests
{
    [Fact]
    public void Read_RealMesh_ReturnsExpectedCoreMetadata()
    {
        var repoRoot = Path.GetFullPath(
            Path.Combine(AppContext.BaseDirectory, "..", "..", "..", "..", "..", "..", ".."));
        var sampleMesh = Path.Combine(
            repoRoot,
            "src",
            "toolkit",
            "model-viewer",
            "build",
            "Release",
            "m006_imperial_lava",
            "fx_lava_fire_splashes_01_pe2_0.nvx2");

        Assert.True(File.Exists(sampleMesh), $"Sample NVX2 file not found: {sampleMesh}");

        var reader = new Nvx2InfoReader();
        var info = reader.Read(sampleMesh);

        Assert.True(info.FileSizeBytes > 0);
        Assert.True(info.NumGroups > 0);
        Assert.True(info.NumVertices > 0);
        Assert.True(info.ExpectedIndexCount > 0);
        Assert.Equal(info.NumGroups, (uint)info.Groups.Count);
        Assert.True(info.Components.Count > 0);
        Assert.Contains(info.InferredIndexElementSizeBytes, new[] { 2, 4 });
    }
}
