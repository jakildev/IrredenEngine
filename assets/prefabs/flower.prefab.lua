-- A composite prefab: the stem is the root's own voxel set and draws at every
-- zoom; the head is three parts on LOD bands. A band is inclusive at both ends
-- and indexed the engine way (LOD_0 finest, LOD_4 coarsest).
--   zoom 1x / 2x   (LOD_4, LOD_3)  stem + silhouette
--   zoom 4x / 8x   (LOD_2, LOD_1)  stem + petals
--   zoom 16x and up (LOD_0)        stem + petals + stamen
-- The petals stay while the stamen appears (additive); the silhouette leaves
-- when the petals arrive (replace). Paths resolve from the executable's
-- directory, where the build stages assets/prefabs.
local L = IRRender.LodLevel

return {
    prefab_version = 2,
    voxel_ref = "assets/prefabs/flower_stem.vxs",
    parts = {
        {
            id = "silhouette",
            voxel_ref = "assets/prefabs/flower_silhouette.vxs",
            transform = { translation = { 0, 0, -7 } },
            lod = { fine = L.LOD_3, coarse = L.LOD_4 },
        },
        {
            id = "petals",
            voxel_ref = "assets/prefabs/flower_petals.vxs",
            transform = { translation = { 0, 0, -7 } },
            lod = { fine = L.LOD_0, coarse = L.LOD_2 },
        },
        {
            id = "stamen",
            shape = {
                type = IRShape.SPHERE,
                params = { 1.5, 1.5, 1.5, 0 },
                color = { r = 250, g = 210, b = 60 },
            },
            transform = { translation = { 0, 0, -8 } },
            lod = { fine = L.LOD_0, coarse = L.LOD_0 },
        },
    },
}
