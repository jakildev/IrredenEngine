/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\rendering\assimp_demo.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// This file is left over from experimenting with meshes
// in the engine with the assimp library. I think it could
// be useful in the future, because I might want to be able
// to convert mesh data into voxel data for the engine, so
// I will leave here for now.

#ifndef ASSIMP_DEMO_H
#define ASSIMP_DEMO_H

#include <assimp/scene.h>

#include <vector>
#include <glm/glm.hpp>

struct MeshData {
    std::vector<glm::vec3> positions;
    std::vector<unsigned int> indices;
    std::vector<unsigned int> indicesLod;
};

MeshData load3DModelPositionsAndIndices(const char* filepath);

#endif /* ASSIMP_DEMO_H */
