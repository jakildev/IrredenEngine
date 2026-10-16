# SDF/voxel profile scene context

These are direct-versus-baked representations on the same source revision,
not before/after screenshots of a rendering change. The
[cost baseline](../../../perf/sdf-voxel-profile/current-stack/README.md)
records the profiling conditions and limitations.

Both captures ran IRShapeDebug on Metal/Debug from
`a371b9f4391b819b20007d1bc05490e727ee8745` with
`--sdf-voxel-scene direct|baked --sdf-voxel-blockers --no-ao --auto-screenshot 10`
through `fleet-run`. Each exited 0 with `RESULT=CLEAN`, producing six native
2560×1440 PNGs. No shader or runtime source was changed for these captures.
The accompanying include cleanup has no runtime effect.

| Pose | Direct SDF | Baked occupancy |
|---|---|---|
| Yaw 0, zoom 4 | [image](direct/yaw0.png) | [image](baked/yaw0.png) |
| Yaw 45, zoom 4 | [image](direct/yaw45.png) | [image](baked/yaw45.png) |
| Yaw 90, zoom 4 | [image](direct/yaw90.png) | [image](baked/yaw90.png) |
| Yaw 180, zoom 4 | [image](direct/yaw180.png) | [image](baked/yaw180.png) |
| Yaw 270, zoom 4 | [image](direct/yaw270.png) | [image](baked/yaw270.png) |
| Yaw 45, zoom 2 | [image](direct/yaw45-zoom2.png) | [image](baked/yaw45-zoom2.png) |

The baked silhouettes retain their authored voxel steps. Default face-center
receiving still gives coarse floor-shadow boundaries; the direct curved surfaces
also retain visible sampling patterns. This record does not approve those as
finished visuals. Continuous receiving remains a separate, opt-in cost problem.
Camera, light, palette and geometry settings are held fixed between the two
representation arms. No image filters or postprocessing were applied.
