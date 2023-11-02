/*
 * Project: Irreden Engine
 * File: system_creator_triangles_only_entity.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Depricated but still some useful stuff here so keeping
// around for now.

// #include "system_creator_triangles_only_entity.hpp"
// #include "../game_systems/system_mouse_input.hpp"
// #include "../game_components/component_velocity_3d.hpp"
// #include "../game_components/component_barcrawl_item.hpp"

// #include "../rendering/image_data.hpp"

// using IRRender::ImageData;

// namespace IRECS {

//     SystemCreatorTrianglesOnlyEntity::SystemCreatorTrianglesOnlyEntity
//     (
//         uvec2 triangleCanvasSize,
//         const char* palletFile
//     )
//         :   m_entityTriangleCanvas{}
//         ,   m_activeColor{}
//         ,   m_mapFaceColors{
//             {X_FACE, C_ColorHSV{{150.0f, 0.45f, 0.30f}}},
//             {Y_FACE, C_ColorHSV{{150.0f, 0.45f, 0.60f}}},
//             {Z_FACE, C_ColorHSV{{150.0f, 0.45f, 0.90f}}}
//         }
//         ,   m_activePaintingMode(TRIANGLES)
//         ,   m_activeFaceType(Z_FACE)
//         ,   m_hasSelection{false}
//         ,   m_trianglePositionStart{ivec2(0)}
//         ,   m_trianglePositionEnd{ivec2(0)}
//         {
//             m_entityTriangleCanvas = EntityTriangleCanvas::create(
//                 triangleCanvasSize,
//                 vec2(0, 0)
//             );
//             // m_entityTriangleCanvas.set(C_Velocity2D{0, 0});
//             if(palletFile) {
//                 this->loadPalette(palletFile);
//             }
//             m_activeColor = m_colorPallet.at(0);
//             global.activeColor_ = &m_activeColor;
//             // this->loadTrianglesFromFile();

//             C_TrianglesOnlySet& triangleSet =
//                 m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//             for(int y = 0; y < triangleSet.size_.y; ++y) {
//                 for(int x = 0; x < triangleSet.size_.x; ++x) {
//                     // LEFT OFF HERE
//                 }
//             }

//             // BARCRAWL ITEMS
//             EntityHandle entityBarcrawlItemBackflip;
//             entityBarcrawlItemBackflip.set(C_Barcrawl_Item{
//                 "Do a backflip off bubblegum wall",
//                 "Double backflip"
//             });


//             IRProfile::engLogInfo(
//                 "Created system triangle painter: palette size={}",
//                 m_colorPallet.size()
//             );
//         }

//         SystemCreatorTrianglesOnlyEntity::~SystemCreatorTrianglesOnlyEntity()
//         {
//             this->saveTrianglesToFile();
//              // m_entityTriangleCanvas.destroy();
//         }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintStart<TRIANGLES>() {
//         C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         // triangleSet.setTriangle(mouseIndex, Color{255, 0, 0, 255});
//         if(glm::all(glm::lessThanEqual(
//                 *global.hoveredTriangleIndexScreen_,
//                 triangleSet.size_ - ivec2(1, 1))) &&
//             glm::all(glm::greaterThanEqual(
//                 *global.hoveredTriangleIndexScreen_,
//                 ivec2(0)))
//         )
//         {
//             triangleSet.setTriangle(
//                 *global.hoveredTriangleIndexScreen_,
//                 m_activeColor
//             );
//         }
//     }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintStart<FACES>() {
//         ivec2 partnerTriangleIndex = ivec2(0, 0);
//         if(m_activeFaceType == X_FACE) {
//             partnerTriangleIndex =
//                 IRMath::calculatePartnerTriangleIndex<X_FACE>(
//                     *global.hoveredTriangleIndexScreen_
//                 );
//         }
//         if(m_activeFaceType == Y_FACE) {
//             partnerTriangleIndex =
//                 IRMath::calculatePartnerTriangleIndex<Y_FACE>(
//                     *global.hoveredTriangleIndexScreen_
//                 );
//         }
//         if(m_activeFaceType == Z_FACE) {
//             partnerTriangleIndex =
//                 IRMath::calculatePartnerTriangleIndex<Z_FACE>(
//                     *global.hoveredTriangleIndexScreen_
//                 );
//         }

//         C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         // triangleSet.setTriangle(triangleIndex, Color{255, 0, 0, 255});
//         triangleSet.setTriangle(*global.hoveredTriangleIndexScreen_, m_activeColor);
//         triangleSet.setTriangle(partnerTriangleIndex, m_activeColor);
//     }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintStart<SELECTION>()
//     {
//         m_trianglePositionStart = *global.hoveredTriangleIndexScreen_;
//     }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintStart<CUBES>()
//     {
//         ivec2 partnerTriangleIndex = ivec2(0, 0);

//         ivec2 indexX = *global.hoveredTriangleIndexScreen_;
//         ivec2 indexXAlt = IRMath::calculatePartnerTriangleIndex<X_FACE>(
//             indexX
//         );

//         ivec2 indexY = indexX - ivec2(1, 0);
//         ivec2 indexYAlt = indexXAlt - ivec2(1, 0);
//         ivec2 indexZ = glm::min(indexY, indexYAlt) - ivec2(0, 1);
//         ivec2 indexZAlt = glm::min(indexX, indexXAlt) - ivec2(0, 1);



//         C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         // triangleSet.setTriangle(triangleIndex, Color{255, 0, 0, 255});
//         triangleSet.setTriangle(indexX, m_colorPallet[m_activeColorIndex]);
//         triangleSet.setTriangle(indexXAlt, m_colorPallet[m_activeColorIndex]);
//         triangleSet.setTriangle(
//             indexY,
//             m_colorPallet[(m_activeColorIndex + 1) % m_colorPallet.size()]
//         );
//         triangleSet.setTriangle(
//             indexYAlt,
//             m_colorPallet[(m_activeColorIndex + 1) % m_colorPallet.size()]
//         );
//         triangleSet.setTriangle(
//             indexZ,
//             m_colorPallet[(m_activeColorIndex - 1) % m_colorPallet.size()]
//         );
//         triangleSet.setTriangle(
//             indexZAlt,
//             m_colorPallet[(m_activeColorIndex - 1) % m_colorPallet.size()]
//         );

//     }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintEnd<SELECTION>()
//     {
//         m_trianglePositionEnd = *global.hoveredTriangleIndexScreen_;
//         ivec2 topLeft = glm::min(m_trianglePositionStart, m_trianglePositionEnd);
//         ivec2 bottomRight = glm::max(m_trianglePositionStart, m_trianglePositionEnd);
//         m_trianglePositionStart = glm::clamp(
//             topLeft,
//             ivec2(0),
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>().size_ - ivec2(1, 1)
//         );
//         m_trianglePositionEnd = glm::clamp(
//             bottomRight,
//             ivec2(0),
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>().size_ - ivec2(1, 1)
//         );
//         m_hasSelection = true;
//         ivec2 selectionSize = m_trianglePositionEnd + ivec2(1, 1) - m_trianglePositionStart;
//         m_triangleSelectionCopy.resize(selectionSize);
//         C_TrianglesOnlySet& triangleSetCanvas =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         for(int y = 0; y < selectionSize.y; ++y) {
//             for(int x = 0; x < selectionSize.x; ++x) {
//                 ivec2 selectionIndex{x, y};
//                 m_triangleSelectionCopy.atTriangleColor(selectionIndex) =
//                     triangleSetCanvas.atTriangleColor(m_trianglePositionStart + ivec2(x, y));
//                 m_triangleSelectionCopy.atTriangleDistance(selectionIndex) =
//                     triangleSetCanvas.atTriangleDistance(m_trianglePositionStart + ivec2(x, y));
//             }
//         }
//         IRProfile::engLogInfo(
//             "Made selection: start={},{}. end={},{}",
//             m_trianglePositionStart.x,
//             m_trianglePositionStart.y,
//             m_trianglePositionEnd.x,
//             m_trianglePositionEnd.y
//         );
//     }

//     template<>
//     void SystemCreatorTrianglesOnlyEntity::paintStart<PASTE_SELECTION>()
//     {
//         C_TrianglesOnlySet& triangleSetCanvas =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         triangleSetCanvas.pasteSet(
//             m_triangleSelectionCopy,
//             *global.hoveredTriangleIndexScreen_
//         );
//     }


//     void SystemCreatorTrianglesOnlyEntity::beginExecute() {
//         IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);

//         // 1. Check for key commands

//         if(global.systemKeyMouseInput_->checkButtonPressed(kKeyButtonEscape)) {
//             m_hasSelection = false;
//         }

//         // Painting mode
//         for(int i = 0; i < kNumKeyCommandsPaintingMode; ++i) {
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyCommandsPaintingModes[i].first))
//             {
//                 m_activePaintingMode = kKeyCommandsPaintingModes[i].second;
//                 IRProfile::engLogInfo(
//                     "Set painting mode={}",
//                     kKeyCommandsPaintingModes[i].second
//                 );
//             }
//         }

//         // Palette color
//         for(int i = 0; i < kNumKeyCommandsPaletteColor; ++i) {
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyCommandsPaletteColor[i].first)) {
//                 m_activeColorIndex = kKeyCommandsPaletteColor[i].second;
//                 m_activeColor = m_colorPallet.at(kKeyCommandsPaletteColor[i].second);
//                 IRProfile::engLogInfo(
//                     "Set active color={}",
//                     kKeyCommandsPaletteColor[i].second
//                 );
//             }
//         }

//         // Face type
//         for(int i = 0; i < kNumKeyCommandsFaceType; ++i)
//         {
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyCommandsFaceType[i].first))
//             {
//                 m_activeFaceType = kKeyCommandsFaceType[i].second;
//                 // m_activeColor = m_mapFaceColors.at(kKeyCommandsFaceType[i].second);
//                 IRProfile::engLogInfo(
//                     "Set active face={}",
//                     kKeyCommandsFaceType[i].second
//                 );
//             }
//         }

//         // if(global.systemKeyMouseInput_->checkButtonPressed(GLFW_KEY_L)) {
//         //     m_entityTriangleCanvas.get<C_Velocity2D>().velocity_ +=
//         //         vec2(0.0f, 0.5f);
//         // }

//         // if(global.checkKeyReleased(GLFW_KEY_L)) {
//         //     m_entityTriangleCanvas.get<C_Velocity2D>().velocity_ -=
//         //         vec2(0.0f, 0.5f);
//         // }

//         if(global.systemKeyMouseInput_->checkButtonDown(
//             kMouseButtonLeft))
//         {
//             if(m_activePaintingMode == FACES)
//             {
//                 paintStart<FACES>();
//             }
//             if(m_activePaintingMode == TRIANGLES)
//             {
//                 paintStart<TRIANGLES>();
//             }
//             if(m_activePaintingMode == CUBES)
//             {
//                 paintStart<CUBES>();
//             }
//         }
//         if(global.systemKeyMouseInput_->checkButtonPressed(
//             IRKeyMouseButtons::kMouseButtonLeft))
//         {
//             if(m_activePaintingMode == SELECTION)
//             {
//                 paintStart<SELECTION>();
//             }
//             if(m_activePaintingMode == SELECTION)
//             {
//                 paintStart<SELECTION>();
//             }
//             if(m_activePaintingMode == PASTE_SELECTION)
//             {
//                 paintStart<PASTE_SELECTION>();
//             }
//         }

//         if(global.systemKeyMouseInput_->checkButtonReleased(
//             IRKeyMouseButtons::kMouseButtonLeft))
//         {
//             if(m_activePaintingMode == SELECTION)
//             {
//                 paintEnd<SELECTION>();
//             }
//         }

//         if(m_hasSelection && m_activePaintingMode == SELECTION) {
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyButtonRight)) {
//                this->moveSelection(ivec2(1, 0));
//             }
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyButtonLeft)) {
//                this->moveSelection(ivec2(-1, 0));
//             }
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyButtonUp)) {
//                this->moveSelection(ivec2(0, -1));
//             }
//             if(global.systemKeyMouseInput_->checkButtonPressed(kKeyButtonDown)) {
//                this->moveSelection(ivec2(0, 1));
//             }
//         }
//     }

//     void SystemCreatorTrianglesOnlyEntity::endExecute() {
//         IRProfile::profileFunction(IR_PROFILER_COLOR_RENDER);

//     }

//     // --------------- Private helpers -------------------

//     void SystemCreatorTrianglesOnlyEntity::moveSelection(ivec2 moveDistance) {
//          C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         triangleSet.moveSelection(
//             m_trianglePositionStart,
//             m_trianglePositionEnd,
//             moveDistance
//         );
//         m_trianglePositionStart = glm::clamp(
//             m_trianglePositionStart + moveDistance,
//             ivec2(0),
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>().size_ - ivec2(1, 1)
//         );
//         m_trianglePositionEnd = glm::clamp(
//             m_trianglePositionEnd + moveDistance,
//             ivec2(0),
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>().size_ - ivec2(1, 1)
//         );
//     }


//     void SystemCreatorTrianglesOnlyEntity::loadPalette(const char* paletteFile)
//     {
        // ImageData d{paletteFile};
//         for(int y = 0; y < d.height_; ++y) {
//             for(int x = 0; x < d.width_; ++x) {
//                 Color color = d.getPixel(x, y);
//                 m_colorPallet.push_back(color);
//             }
//         }
//     }

//     void SystemCreatorTrianglesOnlyEntity::saveTrianglesToFile
//     (
//         const char* filename
//     )
//     {
//         FILE* f = fopen(filename, "wb");
//         C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         fwrite(&triangleSet.size_, sizeof(triangleSet.size_), 1, f);
//         fwrite(&triangleSet.origin_, sizeof(triangleSet.origin_), 1, f);
//         fwrite(
//             triangleSet.triangleColors_.data(),
//             sizeof(triangleSet.triangleColors_.at(0)),
//             triangleSet.size_.x * triangleSet.size_.y,
//             f
//         );
//         fwrite(
//             triangleSet.triangleDistances_.data(),
//             sizeof(triangleSet.triangleDistances_.at(0)),
//             triangleSet.size_.x * triangleSet.size_.y,
//             f
//         );
//         fclose(f);
//         IRProfile::engLogInfo("Saved triangle canvas to {}", filename);
//     }

//     void SystemCreatorTrianglesOnlyEntity::loadTrianglesFromFile(
//         const char* filename
//     )
//     {
//         FILE* f = fopen(filename, "rb");
//         C_TrianglesOnlySet& triangleSet =
//             m_entityTriangleCanvas.get<C_TrianglesOnlySet>();
//         fread(&triangleSet.size_, sizeof(triangleSet.size_), 1, f);
//         fread(&triangleSet.origin_, sizeof(triangleSet.origin_), 1, f);
//         triangleSet.resize();
//         fread(
//             triangleSet.triangleColors_.data(),
//             sizeof(triangleSet.triangleColors_.at(0)),
//             triangleSet.size_.x * triangleSet.size_.y,
//             f
//         );
//         fread(
//             triangleSet.triangleDistances_.data(),
//             sizeof(triangleSet.triangleDistances_.at(0)),
//             triangleSet.size_.x * triangleSet.size_.y,
//             f
//         );
//         fclose(f);
//     }



// } // namespace System
