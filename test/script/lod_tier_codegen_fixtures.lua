-- CODEGEN fixture for `LodTierLua.CodegenSystemReadsActiveTier`: a system that
-- reads the active LOD tier through the `IRRender.getActiveLodTier()` intrinsic
-- and stores it per row, so the test can assert the lowered C++ read tracks the
-- tier LOD_UPDATE writes. `tier` defaults to -1 so a missed write is visible.

IRComponent.register('LodTierCodegenProbe', {
    tier = { type = 'int32', default = -1 },
})

IRSystem.registerSystem({
    name = 'LodTierCodegenRead',
    components = { 'LodTierCodegenProbe' },
    tick = function(arch)
        for i = 0, arch.length - 1 do
            local tier = IRRender.getActiveLodTier()
            arch.LodTierCodegenProbe:setField(i, 'tier', tier)
        end
    end,
})
