using System.Text.Json;
using Microsoft.EntityFrameworkCore;
using Microsoft.Extensions.FileProviders;
using NDEVC.Web.Bridge.Nvx2;
using NDEVC.Web.Api.LinqDemo;
using NDEVC.Web.Api.Runtime;

var builder = WebApplication.CreateBuilder(args);
var linqDemoDatabasePath = Path.Combine(builder.Environment.ContentRootPath, "App_Data", "linq-demo.db");
Directory.CreateDirectory(Path.GetDirectoryName(linqDemoDatabasePath)!);

builder.Services.AddOpenApi();
builder.Services.AddSingleton<Nvx2InfoReader>();
builder.Services.AddSingleton<RuntimeSnapshotStore>();
builder.Services.AddSingleton<RuntimeModelStreamHub>();
builder.Services.AddDbContext<LinqDemoDbContext>(options =>
{
    options.UseSqlite($"Data Source={linqDemoDatabasePath}");
});
builder.Services.AddScoped<LinqExamplesService>();

var app = builder.Build();
await EnsureLinqDemoDatabaseAsync(app);

if (app.Environment.IsDevelopment())
{
    app.MapOpenApi();
}

var cdnRoot = ResolveCdnRoot();
var cdnExtraRoots = ResolveExtraCdnRoots(cdnRoot);
var webRoot = app.Environment.WebRootPath ?? Path.Combine(app.Environment.ContentRootPath, "wwwroot");

// Compatibility: if runtime requests `/some/local/path.ext`, rewrite to `/cdndata/some/local/path.ext`
// unless the file exists in API wwwroot or is already under API routes.
app.Use(async (context, next) =>
{
    if ((HttpMethods.IsGet(context.Request.Method) || HttpMethods.IsHead(context.Request.Method)) &&
        !context.Request.Path.StartsWithSegments("/api") &&
        !context.Request.Path.StartsWithSegments("/cdndata"))
    {
        var rawPath = context.Request.Path.Value ?? string.Empty;
        var relative = rawPath.TrimStart('/');
        if (!string.IsNullOrWhiteSpace(relative))
        {
            var localWebPath = Path.Combine(
                webRoot,
                relative.Replace('/', Path.DirectorySeparatorChar));

            if (!File.Exists(localWebPath))
            {
                context.Request.Path = "/cdndata" + context.Request.Path;
            }
        }
    }

    await next();
});

app.UseDefaultFiles();
app.UseStaticFiles();
if (Directory.Exists(cdnRoot))
{
    app.UseStaticFiles(new StaticFileOptions
    {
        FileProvider = new PhysicalFileProvider(cdnRoot),
        RequestPath = "/cdndata",
        ServeUnknownFileTypes = true,
        DefaultContentType = "application/octet-stream"
    });
}
foreach (var extraRoot in cdnExtraRoots)
{
    if (!Directory.Exists(extraRoot))
    {
        continue;
    }

    app.UseStaticFiles(new StaticFileOptions
    {
        FileProvider = new PhysicalFileProvider(extraRoot),
        RequestPath = "/cdndata",
        ServeUnknownFileTypes = true,
        DefaultContentType = "application/octet-stream"
    });
}
app.UseHttpsRedirection();

app.MapGet("/", () => Results.Redirect("/runtime.html"));

app.MapGet("/api/health", () => Results.Ok(new
{
    status = "ok",
    service = "NDEVC.Web.Api",
    utc = DateTime.UtcNow
}));

app.MapGet("/api/linq/examples", async (
    LinqExamplesService service,
    string? email,
    int? page,
    int? size,
    CancellationToken ct) =>
{
    var result = await service.GetExamplesAsync(email, page, size, ct);
    return Results.Ok(result);
});

app.MapPost("/api/linq/reset", async (LinqDemoDbContext db, CancellationToken ct) =>
{
    await LinqDemoSeeder.ResetAsync(db, ct);
    return Results.Ok(new
    {
        status = "ok",
        users = await db.Users.CountAsync(ct)
    });
});

app.MapGet("/api/runtime/cdn-root", () => Results.Ok(new
{
    cdnRoot,
    cdnExtraRoots
}));

app.MapGet("/api/nvx2/components", () => Results.Ok(Nvx2ComponentCatalog.GetAll()));

app.MapGet("/api/nvx2/info", (string path, Nvx2InfoReader reader) =>
{
    return TryReadNvx2(path, reader);
});

app.MapPost("/api/nvx2/info", (Nvx2InfoRequest request, Nvx2InfoReader reader) =>
{
    return TryReadNvx2(request.Path, reader);
});

app.MapGet("/api/runtime/default-path", (RuntimeSnapshotStore store) => Results.Ok(new
{
    defaultPath = store.GetDefaultSnapshotPath()
}));

app.MapPost("/api/runtime/import", (RuntimeImportRequest request, RuntimeSnapshotStore store, RuntimeModelStreamHub streamHub) =>
{
    var result = store.Import(request.Path);
    if (!result.Success)
    {
        return Results.BadRequest(new
        {
            error = result.Error,
            path = result.ResolvedPath
        });
    }

    streamHub.ReplaceFromSnapshot(result.State);
    return Results.Ok(result.State);
});

app.MapGet("/api/runtime/current", (RuntimeSnapshotStore store) =>
{
    var state = store.GetCurrent();
    if (state is null)
    {
        store.TryAutoImport();
        state = store.GetCurrent();
    }

    if (state is null)
    {
        return Results.NotFound(new
        {
            error = "No runtime snapshot loaded yet.",
            hint = "POST /api/runtime/import or write snapshot from C++ runtime first.",
            defaultPath = store.GetDefaultSnapshotPath()
        });
    }

    return Results.Ok(state);
});

app.MapPost("/api/runtime/events", (RuntimeModelEventRequest request, RuntimeModelStreamHub streamHub) =>
{
    if (!RuntimeModelStreamHub.IsValidEventType(request.Type))
    {
        return Results.BadRequest(new
        {
            error = $"Unsupported event type '{request.Type}'.",
            supported = new[]
            {
                RuntimeModelStreamHub.EventTypeModelLoaded,
                RuntimeModelStreamHub.EventTypeModelUnloaded,
                RuntimeModelStreamHub.EventTypeReset
            }
        });
    }

    var isReset = string.Equals(
        request.Type,
        RuntimeModelStreamHub.EventTypeReset,
        StringComparison.OrdinalIgnoreCase);
    if (!isReset &&
        string.IsNullOrWhiteSpace(request.MeshResourceId) &&
        string.IsNullOrWhiteSpace(request.ModelPath))
    {
        return Results.BadRequest(new
        {
            error = "Either meshResourceId or modelPath is required for model events."
        });
    }

    var evt = streamHub.Publish(request);
    return Results.Ok(evt);
});

app.MapGet("/api/runtime/models", (RuntimeModelStreamHub streamHub) =>
{
    return Results.Ok(streamHub.GetSnapshot());
});

app.MapGet("/api/runtime/stream", async (HttpContext context, RuntimeModelStreamHub streamHub, long? afterEventId) =>
{
    context.Response.Headers.ContentType = "text/event-stream";
    context.Response.Headers.CacheControl = "no-cache";
    context.Response.Headers.Connection = "keep-alive";
    context.Response.Headers.Append("X-Accel-Buffering", "no");

    var ct = context.RequestAborted;
    var effectiveAfterId = afterEventId ?? ParseLastEventId(context.Request.Headers["Last-Event-ID"]);
    var backlog = streamHub.GetEventsSince(effectiveAfterId, 2000);
    foreach (var evt in backlog)
    {
        await WriteSseEventAsync(context.Response, evt, ct);
    }

    await context.Response.WriteAsync(": connected\n\n", ct);
    await context.Response.Body.FlushAsync(ct);

    var subscription = streamHub.Subscribe();
    try
    {
        await foreach (var evt in subscription.Reader.ReadAllAsync(ct))
        {
            await WriteSseEventAsync(context.Response, evt, ct);
        }
    }
    catch (OperationCanceledException)
    {
        // client disconnected
    }
    finally
    {
        streamHub.Unsubscribe(subscription.Id);
    }
});

app.Run();

static IResult TryReadNvx2(string path, Nvx2InfoReader reader)
{
    if (string.IsNullOrWhiteSpace(path))
    {
        return Results.BadRequest(new { error = "Path is required." });
    }

    try
    {
        var info = reader.Read(path);
        return Results.Ok(info);
    }
    catch (FileNotFoundException ex)
    {
        return Results.NotFound(new { error = ex.Message, path = ex.FileName ?? path });
    }
    catch (InvalidDataException ex)
    {
        return Results.BadRequest(new { error = ex.Message, path });
    }
    catch (Exception ex)
    {
        return Results.Problem(
            detail: ex.Message,
            statusCode: StatusCodes.Status500InternalServerError,
            title: "Failed to read NVX2 file.");
    }
}

static async Task WriteSseEventAsync(HttpResponse response, RuntimeModelEvent evt, CancellationToken ct)
{
    var json = JsonSerializer.Serialize(evt);
    await response.WriteAsync($"id: {evt.EventId}\n", ct);
    await response.WriteAsync("event: runtime-model\n", ct);
    await response.WriteAsync($"data: {json}\n\n", ct);
    await response.Body.FlushAsync(ct);
}

static long ParseLastEventId(string? raw)
{
    return long.TryParse(raw, out var value) && value > 0 ? value : 0;
}

static string ResolveCdnRoot()
{
    var env = Environment.GetEnvironmentVariable("NDEVC_CDN_ROOT");
    if (!string.IsNullOrWhiteSpace(env))
    {
        return Path.GetFullPath(env);
    }

    foreach (var start in new[]
    {
        Directory.GetCurrentDirectory(),
        AppContext.BaseDirectory
    })
    {
        var root = TryFindWorkspaceRoot(start);
        if (!string.IsNullOrWhiteSpace(root))
        {
            return root;
        }
    }

    return Directory.GetCurrentDirectory();
}

static List<string> ResolveExtraCdnRoots(string primaryRoot)
{
    var roots = new List<string>();
    var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        Path.GetFullPath(primaryRoot)
    };

    var envExtra = Environment.GetEnvironmentVariable("NDEVC_CDN_ROOT_EXTRA");
    if (!string.IsNullOrWhiteSpace(envExtra))
    {
        foreach (var raw in envExtra.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var full = Path.GetFullPath(raw);
            if (seen.Add(full))
            {
                roots.Add(full);
            }
        }
    }

    try
    {
        foreach (var drive in DriveInfo.GetDrives())
        {
            if (!drive.IsReady)
            {
                continue;
            }

            var drasa = Path.Combine(drive.RootDirectory.FullName, "drasa_online", "work");
            if (!Directory.Exists(drasa))
            {
                continue;
            }

            var root = Path.GetFullPath(drive.RootDirectory.FullName);
            if (seen.Add(root))
            {
                roots.Add(root);
            }
        }
    }
    catch
    {
        // Ignore drive enumeration failures and keep existing roots.
    }

    return roots;
}

static string? TryFindWorkspaceRoot(string startDirectory)
{
    if (string.IsNullOrWhiteSpace(startDirectory))
    {
        return null;
    }

    var current = new DirectoryInfo(Path.GetFullPath(startDirectory));
    while (current is not null)
    {
        var marker = Path.Combine(current.FullName, "src", "web", "content");
        if (Directory.Exists(marker))
        {
            return current.FullName;
        }

        current = current.Parent;
    }

    return null;
}

static async Task EnsureLinqDemoDatabaseAsync(WebApplication app)
{
    using var scope = app.Services.CreateScope();
    var db = scope.ServiceProvider.GetRequiredService<LinqDemoDbContext>();
    await LinqDemoSeeder.EnsureSeededAsync(db);
}

public sealed class Nvx2InfoRequest
{
    public string Path { get; set; } = string.Empty;
}
