# NVX2 API Contract (MVP)

Version: `v1`

## Resources

- `GET /api/health`
- `GET /api/nvx2/components`
- `GET /api/nvx2/info?path=<path>`
- `POST /api/nvx2/info`

## Request/Response Notes

### `GET /api/nvx2/info`

Query:
- `path`: absolute or relative file path to `.nvx2`

### `POST /api/nvx2/info`

Body:

```json
{ "path": "bin/meshes/example.nvx2" }
```

### Successful Response Fields (subset)

- `fileSizeBytes`
- `numGroups`
- `numVertices`
- `componentMask`
- `components[]` with `name`, `bit`, `offsetBytes`, `sizeBytes`
- `groups[]` with per-group triangle/index info
- `expectedIndexCount`
- `isLikely32BitIndices`
- `inferredIndexElementSizeBytes`
- `warnings[]`

## Error Behavior

- `400`: invalid path or invalid NVX2 structure
- `404`: file does not exist
- `500`: unexpected server error
