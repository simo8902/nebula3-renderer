using NDEVC.Web.Api.Runtime;

namespace NDEVC.Web.Tests;

public class RuntimeModelStreamHubTests
{
    [Fact]
    public void Publish_ModelLoaded_AddsModelToSnapshot()
    {
        var hub = new RuntimeModelStreamHub();

        var evt = hub.Publish(new RuntimeModelEventRequest
        {
            Type = "model_loaded",
            MeshResourceId = "mesh/a",
            ModelPath = @"C:\assets\mesh_a.nvx2"
        });

        var snapshot = hub.GetSnapshot();

        Assert.Equal(RuntimeModelStreamHub.EventTypeModelLoaded, evt.Type);
        Assert.Equal(1, snapshot.Count);
        Assert.Equal(1, snapshot.LastEventId);
        Assert.Equal("mesh/a", snapshot.Models[0].MeshResourceId);
        Assert.Equal(@"C:\assets\mesh_a.nvx2", snapshot.Models[0].ModelPath);
    }

    [Fact]
    public void Publish_ModelUnloaded_RemovesModelFromSnapshot()
    {
        var hub = new RuntimeModelStreamHub();
        hub.Publish(new RuntimeModelEventRequest
        {
            Type = "model_loaded",
            MeshResourceId = "mesh/a",
            ModelPath = @"C:\assets\mesh_a.nvx2"
        });

        var evt = hub.Publish(new RuntimeModelEventRequest
        {
            Type = "model_unloaded",
            MeshResourceId = "mesh/a"
        });

        var snapshot = hub.GetSnapshot();

        Assert.Equal(RuntimeModelStreamHub.EventTypeModelUnloaded, evt.Type);
        Assert.Equal(0, snapshot.Count);
        Assert.Equal(2, snapshot.LastEventId);
    }

    [Fact]
    public void Publish_Reset_ClearsSnapshot()
    {
        var hub = new RuntimeModelStreamHub();
        hub.Publish(new RuntimeModelEventRequest
        {
            Type = "model_loaded",
            MeshResourceId = "mesh/a",
            ModelPath = @"C:\assets\mesh_a.nvx2"
        });
        hub.Publish(new RuntimeModelEventRequest
        {
            Type = "model_loaded",
            MeshResourceId = "mesh/b",
            ModelPath = @"C:\assets\mesh_b.nvx2"
        });

        hub.Publish(new RuntimeModelEventRequest { Type = "reset" });
        var snapshot = hub.GetSnapshot();

        Assert.Equal(0, snapshot.Count);
        Assert.Equal(3, snapshot.LastEventId);
    }
}
