local animation_duration = 5.0

IREntity.createEntityBatchVoxelPeriodicIdle(
    ivec3.new(8, 8, 8),
    function(params)
        local index = params.index
        local basePosition = vec3.new(index.x, index.y, index.z) - params.center
        return C_Position3D.new(
            vec3.new(
                basePosition.x * basePosition.z + basePosition.y * 3,
                basePosition.y * 10,
                0
            )
        )
    end,
    function(params)
        local index = params.index
        return C_VoxelSetNew.new(
            ivec3.new(1, 1, 1),
            Color.new(index.x * 8, 0, 20 + index.y * 2, 255)
        )
    end,
    function(params)
        local index = params.index
        idle_component = C_PeriodicIdle.new(
            100,
            5.0,
            (index.x + index.y) / 128.0 * math.pi * 2
        )
        idle_component:addStageDurationSeconds(
            0.0,
            animation_duration,
            0,
            1,
            IREasingFunction.SINE_EASE_IN_OUT
        )
        return idle_component
    end
)