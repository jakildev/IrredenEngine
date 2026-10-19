# engine/catalog/ — read-only creation and asset discovery

The catalog is a setup-time filesystem service. It depends only on `asset/`,
`utility/`, and `common/`; keep ECS, rendering, world, and prefab runtime
dependencies out.

All caller paths are relative to the service's trusted tool root and pass
through `IRUtility::resolveContainedPath` before any read. Returned paths are
also tool-root-relative so Lua can pass them directly to other contained-path
services.

The scan never follows symlinks or descends into `.git`, `.claude`,
`node_modules`, or a directory whose name starts with `build`.
