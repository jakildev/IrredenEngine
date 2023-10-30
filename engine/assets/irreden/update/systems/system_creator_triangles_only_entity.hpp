/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_creator_triangles_only_entity.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Depricated but still some useful stuff here so keeping
// around for now.

// #ifndef SYSTEM_TRIANGLE_PAINTER_H
// #define SYSTEM_TRIANGLE_PAINTER_H

// #include "..\world\global.hpp"
// #include <irreden/ir_constants.hpp>
// // #include "..\world\glfw_helper.hpp"
// #include "../systems/system.hpp"
// #include <irreden/ecs/entity_handle.hpp>
// #include "..\input\ir_input.hpp"

// #include "../game_entities/entity_triangle_canvas.hpp"
// #include "..\game_components\component_keyboard_key_status.hpp"
// #include "..\game_components\component_color_hsva.hpp"
// #include "..\game_components\component_triangles_only_set.hpp"

// #include <utility> // std::pair

// using namespace IRComponents;
// using namespace IRMath;
// using namespace IRECS;
// using namespace IRInput;

// namespace IRECS {

//     enum EnumPaintingModes {
//         TRIANGLES,
//         FACES,
//         CUBES,
//         SELECTION,
//         PASTE_SELECTION
//     };

//     constexpr std::pair<IRKeyMouseButtons, EnumPaintingModes> kKeyCommandsPaintingModes[] = {
//         {kKeyButtonT, TRIANGLES},
//         {kKeyButtonF, FACES},
//         {kKeyButtonC, CUBES},
//         {kKeyButtonS, SELECTION},
//         {kKeyButtonP, PASTE_SELECTION}
//     };
//     constexpr int kNumKeyCommandsPaintingMode =
//         sizeof(kKeyCommandsPaintingModes) /
//         sizeof(kKeyCommandsPaintingModes[0])
//     ;

//     constexpr std::pair<IRKeyMouseButtons, int> kKeyCommandsPaletteColor[] = {
//         {kKeyButton1, 0},
//         {kKeyButton2, 1},
//         {kKeyButton3, 2},
//         {kKeyButton4, 3},
//         {kKeyButton5, 4},
//         {kKeyButton6, 5}
//     };
//     constexpr int kNumKeyCommandsPaletteColor =
//         sizeof(kKeyCommandsPaletteColor) /
//         sizeof(kKeyCommandsPaletteColor[0]);

//     constexpr std::pair<IRKeyMouseButtons, FaceType> kKeyCommandsFaceType[] = {
//         {kKeyButtonX, FaceType::X_FACE},
//         {kKeyButtonY, FaceType::Y_FACE},
//         {kKeyButtonZ, FaceType::Z_FACE}
//     };
//     constexpr int kNumKeyCommandsFaceType =
//         sizeof(kKeyCommandsFaceType) /
//         sizeof(kKeyCommandsFaceType[0])
//     ;

//     class SystemCreatorTrianglesOnlyEntity {
//     public:
//         SystemCreatorTrianglesOnlyEntity
//         (
//             uvec2 triangleCanvasSize = IRConstants::kScreenTriangleMaxCanvasSize,
//             const char* palletFile = nullptr
//         );
//         ~SystemCreatorTrianglesOnlyEntity();

//         void beginExecute();
//         void endExecute();
//     private:
//         EntityHandle m_entityTriangleCanvas;
//         // TODO: create/implement single component entity
//         // and make this a reference to a component/entity
//         int m_activeColorIndex;
//         Color m_activeColor;
//         C_ColorHSV m_activeColorHSV;
//         std::unordered_map<FaceType, C_ColorHSV> m_mapFaceColors;
//         EnumPaintingModes m_activePaintingMode;
//         FaceType m_activeFaceType;

//         // C_FaceType m_faceType;
//         // C_Distance m_activeDistanceStart;
//         bool m_hasSelection;
//         ivec2 m_trianglePositionStart;
//         ivec2 m_trianglePositionEnd;
//         std::vector<Color> m_colorPallet;
//         C_TrianglesOnlySet m_triangleSelectionCopy;

//         template <EnumPaintingModes mode>
//         void paintStart();

//         template <EnumPaintingModes mode>
//         void paintEnd();

//         void moveSelection(ivec2 moveDistance);
//         void loadPalette(const char* paletteFile);
//         void saveTrianglesToFile(const char* filename = "currentTriangleProject.irt");
//         void loadTrianglesFromFile(const char* filename = "currentTriangleProject.irt");


//     };

// } // namespace IRSystem



// #endif /* SYSTEM_TRIANGLE_PAINTER_H */
