// #include "assimp_demo.hpp"

// #include <assimp/scene.h>
// #include <assimp/postprocess.h>
// #include <assimp/cimport.h>
// #include <assimp/version.h>
// #include <meshoptimizer.h>
// #include <glm/glm.hpp>
// #include <glm/ext.hpp>
// #include <irreden/ir_profile.hpp>

// // "data/rubber_duck/scene.gltf"

// // TODO: Test this out once its place in pipeline is ready
// // Adapted from https://github.com/PacktPublishing/3D-Graphics-Rendering-Cookbook
// // This uses lod optimization, likely useful stuff here
// MeshData load3DModelPositionsAndIndices(const char* filepath) {
//     MeshData meshData;

//     const aiScene* scene = aiImportFile(
//         filepath,
//         aiProcess_Triangulate);
//     IR_ASSERT(scene, "Unable to load file.");
//     IR_ASSERT(scene->HasMeshes(), "No meshes??? (like the meme)");
//     const aiMesh* mesh = scene->mMeshes[0];

//     std::vector<glm::vec3> positions;
//     std::vector<unsigned int> indices;
//     const int positionSize = 3;

//     for(int i=0; i != mesh->mNumVertices; i++) {
//         const aiVector3D v = mesh->mVertices[i];
//         positions.push_back(glm::vec3(v.x, v.z, v.y));
//     }

//     for (int i=0; i != mesh->mNumFaces; i++) {
//         for (int j=0; j != positionSize; j++) {
//             indices.push_back(mesh->mFaces[i].mIndices[j]);
//         }
//     }
//     aiReleaseImport(scene);

//     std::vector<unsigned int> indicesLod;

//     std::vector<unsigned int> remap(indices.size());
//     const size_t vertexCount = meshopt_generateVertexRemap(
//                                     remap.data(),
//                                     indices.data(),
//                                     indices.size(),
//                                     positions.data(),
//                                     indices.size(),
//                                     sizeof(glm::vec3));

//     std::vector<unsigned int> remappedIndices(indices.size());
//     std::vector<glm::vec3> remappedVertices(vertexCount);

//     meshopt_remapIndexBuffer(
//         remappedIndices.data(),
//         indices.data(),
//         indices.size(),
//         remap.data());
//     meshopt_remapVertexBuffer(
//         remappedVertices.data(),
//         positions.data(),
//         positions.size(),
//         sizeof(glm::vec3),
//         remap.data());

//     meshopt_optimizeVertexCache(
//         remappedIndices.data(),
//         remappedIndices.data(),
//         indices.size(),
//         vertexCount);
//     meshopt_optimizeOverdraw(
//         remappedIndices.data(),
//         remappedIndices.data(),
//         indices.size(),
//         glm::value_ptr(remappedVertices[0]),
//         vertexCount, sizeof(glm::vec3),
//         1.05f);
//     meshopt_optimizeVertexFetch(
//         remappedVertices.data(),
//         remappedIndices.data(),
//         indices.size(),
//         remappedVertices.data(),
//         vertexCount,
//         sizeof(glm::vec3));

//     const float threshold = 0.2f;
//     const size_t target_index_count = size_t(remappedIndices.size() * threshold);
//     const float target_error = 1e-2f;

//     indicesLod.resize(remappedIndices.size());
//     indicesLod.resize(
//         meshopt_simplify(
//             &indicesLod[0],
//             remappedIndices.data(),
//             remappedIndices.size(),
//             &remappedVertices[0].x,
//             vertexCount,
//             sizeof(glm::vec3),
//             target_index_count,
//             target_error));

//     meshData.indices = remappedIndices;
//     meshData.indicesLod = indicesLod;
//     meshData.positions = remappedVertices;

//     return meshData;
// }