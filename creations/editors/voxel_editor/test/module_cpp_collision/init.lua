-- Failing fixture for IRVoxelEditor --module: registers a name the editor
-- already binds as a C++ component, so the launch must exit 2.

IRComponent.register("C_LocalTransform", { weight = 1 })
