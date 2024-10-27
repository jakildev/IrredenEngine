# Ideas: ECS

```
C_Position2D => C_Position3D // depends on???

```
```
entity.set(C_Position2D, [](C_Position3D position3D)
        { newComponents.pos_ = })
    // Essentially lambda function for a component that defines
    // how it interacts with other components each frame
```

REAL TIME ANIMATOR/RECORDER

Why not just generate files with C++ components?
Should be the compilation phase of lua files?