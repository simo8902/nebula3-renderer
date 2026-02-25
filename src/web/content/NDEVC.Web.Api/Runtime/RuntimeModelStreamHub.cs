using System.Collections.Concurrent;
using System.Threading.Channels;

namespace NDEVC.Web.Api.Runtime;

public sealed class RuntimeModelEventRequest
{
    public string Type { get; set; } = "model_loaded";
    public string? MeshResourceId { get; set; }
    public string? ModelPath { get; set; }
    public DateTime? TimestampUtc { get; set; }
    public Dictionary<string, string>? Meta { get; set; }
}

public sealed class RuntimeModelEvent
{
    public long EventId { get; set; }
    public string Type { get; set; } = string.Empty;
    public DateTime TimestampUtc { get; set; }
    public string MeshResourceId { get; set; } = string.Empty;
    public string ModelPath { get; set; } = string.Empty;
    public RuntimeModelState? Model { get; set; }
    public Dictionary<string, string>? Meta { get; set; }
}

public sealed class RuntimeModelState
{
    public string Key { get; set; } = string.Empty;
    public string MeshResourceId { get; set; } = string.Empty;
    public string ModelPath { get; set; } = string.Empty;
    public DateTime FirstSeenUtc { get; set; }
    public DateTime LastSeenUtc { get; set; }
    public int SeenCount { get; set; }
}

public sealed class RuntimeModelSnapshot
{
    public long LastEventId { get; set; }
    public int Count => Models.Count;
    public List<RuntimeModelState> Models { get; set; } = [];
}

public sealed class RuntimeModelStreamHub
{
    public const string EventTypeModelLoaded = "model_loaded";
    public const string EventTypeModelUnloaded = "model_unloaded";
    public const string EventTypeReset = "reset";

    private const int MaxBufferedEvents = 5000;

    private readonly object gate = new();
    private readonly Dictionary<string, RuntimeModelState> modelsByKey = new(StringComparer.OrdinalIgnoreCase);
    private readonly List<RuntimeModelEvent> eventBuffer = [];
    private readonly ConcurrentDictionary<Guid, Channel<RuntimeModelEvent>> subscribers = new();

    private long nextEventId;

    public static bool IsValidEventType(string? type)
    {
        var normalized = NormalizeField(type).ToLowerInvariant();
        return normalized is
            "load" or "loaded" or "model_loaded" or
            "unload" or "unloaded" or "model_unloaded" or
            "reset";
    }

    public long CurrentEventId
    {
        get
        {
            lock (gate)
            {
                return nextEventId;
            }
        }
    }

    public RuntimeModelEvent Publish(RuntimeModelEventRequest request)
    {
        var type = NormalizeType(request.Type);
        var meshResourceId = NormalizeField(request.MeshResourceId);
        var modelPath = NormalizeField(request.ModelPath);
        var timestamp = request.TimestampUtc?.ToUniversalTime() ?? DateTime.UtcNow;

        RuntimeModelState? modelState = null;
        RuntimeModelState? removedState = null;
        RuntimeModelEvent evt;

        lock (gate)
        {
            if (type == EventTypeReset)
            {
                modelsByKey.Clear();
            }
            else
            {
                var key = BuildModelKey(meshResourceId, modelPath);
                if (!string.IsNullOrEmpty(key))
                {
                    if (type == EventTypeModelUnloaded)
                    {
                        if (modelsByKey.TryGetValue(key, out var existing))
                        {
                            removedState = CloneModelState(existing);
                            modelsByKey.Remove(key);
                        }
                    }
                    else
                    {
                        if (!modelsByKey.TryGetValue(key, out var existing))
                        {
                            existing = new RuntimeModelState
                            {
                                Key = key,
                                FirstSeenUtc = timestamp
                            };
                            modelsByKey[key] = existing;
                        }

                        existing.MeshResourceId = meshResourceId;
                        existing.ModelPath = modelPath;
                        existing.LastSeenUtc = timestamp;
                        existing.SeenCount++;
                        modelState = CloneModelState(existing);
                    }
                }
            }

            evt = new RuntimeModelEvent
            {
                EventId = ++nextEventId,
                Type = type,
                TimestampUtc = timestamp,
                MeshResourceId = meshResourceId,
                ModelPath = modelPath,
                Model = type == EventTypeModelUnloaded ? removedState : modelState,
                Meta = request.Meta
            };

            eventBuffer.Add(evt);
            if (eventBuffer.Count > MaxBufferedEvents)
            {
                eventBuffer.RemoveRange(0, eventBuffer.Count - MaxBufferedEvents);
            }
        }

        Broadcast(evt);
        return evt;
    }

    public RuntimeModelSnapshot GetSnapshot()
    {
        lock (gate)
        {
            var models = modelsByKey.Values
                .Select(CloneModelState)
                .OrderBy(m => m.MeshResourceId, StringComparer.OrdinalIgnoreCase)
                .ThenBy(m => m.ModelPath, StringComparer.OrdinalIgnoreCase)
                .ToList();

            return new RuntimeModelSnapshot
            {
                LastEventId = nextEventId,
                Models = models
            };
        }
    }

    public List<RuntimeModelEvent> GetEventsSince(long afterEventId, int maxCount = 1000)
    {
        if (maxCount < 1) maxCount = 1;

        lock (gate)
        {
            return eventBuffer
                .Where(e => e.EventId > afterEventId)
                .OrderBy(e => e.EventId)
                .Take(maxCount)
                .Select(CloneEvent)
                .ToList();
        }
    }

    public (Guid Id, ChannelReader<RuntimeModelEvent> Reader) Subscribe()
    {
        var channel = Channel.CreateUnbounded<RuntimeModelEvent>(new UnboundedChannelOptions
        {
            SingleReader = true,
            SingleWriter = false
        });
        var id = Guid.NewGuid();
        subscribers[id] = channel;
        return (id, channel.Reader);
    }

    public void Unsubscribe(Guid subscriptionId)
    {
        if (subscribers.TryRemove(subscriptionId, out var channel))
        {
            channel.Writer.TryComplete();
        }
    }

    public void ReplaceFromSnapshot(RuntimeSnapshotState? state)
    {
        if (state?.Snapshot is null)
        {
            return;
        }

        Publish(new RuntimeModelEventRequest
        {
            Type = EventTypeReset,
            TimestampUtc = DateTime.UtcNow
        });

        foreach (var mesh in state.Snapshot.Meshes)
        {
            Publish(new RuntimeModelEventRequest
            {
                Type = EventTypeModelLoaded,
                MeshResourceId = mesh.MeshResourceId,
                ModelPath = mesh.MeshPath,
                TimestampUtc = DateTime.UtcNow
            });
        }
    }

    private static RuntimeModelEvent CloneEvent(RuntimeModelEvent evt)
    {
        return new RuntimeModelEvent
        {
            EventId = evt.EventId,
            Type = evt.Type,
            TimestampUtc = evt.TimestampUtc,
            MeshResourceId = evt.MeshResourceId,
            ModelPath = evt.ModelPath,
            Model = evt.Model is null ? null : CloneModelState(evt.Model),
            Meta = evt.Meta is null ? null : new Dictionary<string, string>(evt.Meta, StringComparer.Ordinal)
        };
    }

    private void Broadcast(RuntimeModelEvent evt)
    {
        var snapshot = CloneEvent(evt);
        foreach (var subscriber in subscribers.Values)
        {
            subscriber.Writer.TryWrite(snapshot);
        }
    }

    private static RuntimeModelState CloneModelState(RuntimeModelState model)
    {
        return new RuntimeModelState
        {
            Key = model.Key,
            MeshResourceId = model.MeshResourceId,
            ModelPath = model.ModelPath,
            FirstSeenUtc = model.FirstSeenUtc,
            LastSeenUtc = model.LastSeenUtc,
            SeenCount = model.SeenCount
        };
    }

    private static string BuildModelKey(string meshResourceId, string modelPath)
    {
        if (!string.IsNullOrEmpty(meshResourceId))
        {
            return "res:" + meshResourceId;
        }

        if (!string.IsNullOrEmpty(modelPath))
        {
            return "path:" + modelPath;
        }

        return string.Empty;
    }

    private static string NormalizeType(string? type)
    {
        var normalized = NormalizeField(type).ToLowerInvariant();
        return normalized switch
        {
            "load" => EventTypeModelLoaded,
            "loaded" => EventTypeModelLoaded,
            "model_loaded" => EventTypeModelLoaded,
            "unload" => EventTypeModelUnloaded,
            "unloaded" => EventTypeModelUnloaded,
            "model_unloaded" => EventTypeModelUnloaded,
            "reset" => EventTypeReset,
            _ => EventTypeModelLoaded
        };
    }

    private static string NormalizeField(string? value)
    {
        return string.IsNullOrWhiteSpace(value) ? string.Empty : value.Trim();
    }
}
