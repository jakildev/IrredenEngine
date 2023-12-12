// /*
//  * Project: Irreden Engine
//  * File: system_camera.hpp
//  * Author: Evin Killian jakildev@gmail.com
//  * Created Date: December 2023
//  * -----
//  * Modified By: <your_name> <Month> <YYYY>
//  */

// #ifndef SYSTEM_CAMERA_H
// #define SYSTEM_CAMERA_H

// #include <irreden/ir_ecs.hpp>

// #include <irreden/render/components/component_camera.hpp>
// #include <irreden/render/components/component_zoom_level.hpp>
// #include <irreden/common/components/component_position_2d_iso.hpp>
// #include <irreden/update/components/component_velocity_2d_iso.hpp>

// namespace IRECS {

//     template <>
//     struct System<CAMERA> {
//         static SystemId create() {
//             return createSystem<
//                 C_Camera,
//                 C_ZoomLevel,
//                 C_Position2DIso,
//                 C_Velocity2DIso
//             >
//             (
//                 "Camera",
//                 [](
//                     const C_Camera& camera,
//                     const C_ZoomLevel& zoomLevel,
//                     C_Position2DIso& position,
//                     C_Velocity2DIso& velocity
//                 )
//                 {

//                 }
//             );

//         }
//     }

// } // namespace IRECS

// #endif /* SYSTEM_CAMERA_H */
