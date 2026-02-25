# NDEVC.Web.Api

## Runtime Stream Endpoints

- `POST /api/runtime/events`
- `GET /api/runtime/models`
- `GET /api/runtime/stream` (Server-Sent Events)

### Event Payload

```json
{
  "type": "model_loaded",
  "meshResourceId": "m006_imperial_lava/fx_lava_fire_splashes_01_pe2_0",
  "modelPath": "C:/assets/meshes/fx_lava_fire_splashes_01_pe2_0.nvx2"
}
```

Supported `type` values:
- `model_loaded`
- `model_unloaded`
- `reset`

For `model_loaded`/`model_unloaded`, at least one of `meshResourceId` or `modelPath` is required.

## Browser Pages

- `/runtime.html` snapshot inspector
- `/stream.html` live model stream page
- `/linq-demo.html` EF Core LINQ operator examples (real Sqlite-backed results)

## LINQ + EF Core Demo Endpoints

- `GET /api/linq/examples?email=alice@example.com&page=1&size=2`
- `POST /api/linq/reset`

`/api/linq/examples` executes and returns real usage for:
- `Where`
- `Select`
- `SelectMany`
- `OrderBy`
- `OrderByDescending`
- `ThenBy`
- `GroupBy`
- `Join`
- `Any`
- `All`
- `FirstOrDefault`
- `SingleOrDefault`
- `Count`
- `Skip`
- `Take`
- `Sum`
- `Min/Max`
- `Include`
- `ToList`

## Local File HTTP Route

- `GET /cdndata/{relative-path}`

Default root:
- repository root (auto-detected)

Override:
- `NDEVC_CDN_ROOT`

## C++ Auto-Import Hook

`DeferredRenderer::WriteWebSnapshot(...)` now auto-posts:
- `POST /api/runtime/import`

Defaults:
- URL: `http://localhost:5164/api/runtime/import`

Overrides:
- `NDEVC_WEB_IMPORT_URL` to change target URL
- `NDEVC_WEB_IMPORT_DISABLE=1` to disable auto POST from C++

Model-load runtime traffic from engine:
- POST model events: `NDEVC_WEB_EVENTS_URL` (default: `http://localhost:5164/api/runtime/events`)
- GET local files through CDN route: `NDEVC_WEB_CDN_URL` (default: `http://localhost:5164/cdndata/`)
- Base URL helper: `NDEVC_WEB_API_BASE` (default: `http://localhost:5164`)
- Disable model event posts: `NDEVC_WEB_EVENTS_DISABLE=1`
- Disable model CDN GETs: `NDEVC_WEB_CDN_GET_DISABLE=1`
