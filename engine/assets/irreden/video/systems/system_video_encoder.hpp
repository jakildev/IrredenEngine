// /*
//  * Project: Irreden Engine
//  * File: system_video_encoder.hpp
//  * Author: Evin Killian jakildev@gmail.com
//  * Created Date: October 2023
//  * -----
//  * Modified By: <your_name> <Month> <YYYY>
//  */

// #ifndef SYSTEM_VIDEO_ENCODER_H
// #define SYSTEM_VIDEO_ENCODER_H


// #include <irreden/ir_constants.hpp>
// #include <irreden/system/system_base.hpp>
// #include <irreden/ir_render.hpp>
// #include <irreden/ir_math.hpp>

// #include <dtv/dtv.h>

// using namespace IRRender;
// using namespace IRMath;

// namespace IRECS {

//     // conste
//     constexpr ivec2 kVideoEncoderDefaultResolution =
//         IRConstants::kInitWindowSize;
//     constexpr GLenum kDataGLFormat = GL_RGB;
//     constexpr GLenum kDataGLType = GL_UNSIGNED_BYTE;

//     template<>
//     class System<VIDEO_ENCODER> : public SystemBase<
//         VIDEO_ENCODER
//     >  {
//     public:
//         System(
//             const std::string outfile = "video_encoder_out.mp4",
//             ivec2 inOutResolution = kVideoEncoderDefaultResolution,
//             GLenum inOutFormat = kDataGLFormat,
//             GLenum inOutType = kDataGLType
//         )
//         :   m_inOutResolution{inOutResolution}
//         ,   m_GLFormat{inOutFormat}
//         ,   m_GLType{inOutType}
//         ,   m_outbuffer{}
//         // ,   m_flippedPixels{}
//         ,   m_dtvEncoder{}
//         ,   m_encoderSettings{}
//         {
//             m_outbuffer.resize(
//                 m_inOutResolution.x *
//                 m_inOutResolution.y *
//                 3,
//                 0
//             );
//             // m_flippedPixels.resize(
//             //     m_inOutResolution.x *
//             //     m_inOutResolution.y *
//             //     3,
//             //     0
//             // );
//             m_encoderSettings.inputWidth = m_inOutResolution.x;
//             m_encoderSettings.inputHeight = m_inOutResolution.y;
//             m_encoderSettings.width = m_inOutResolution.x;
//             m_encoderSettings.height = m_inOutResolution.y;
//             m_encoderSettings.frameRate = IRConstants::kFPS;
//             m_encoderSettings.hardwareEncoding = true;
//             m_encoderSettings.bitRate = 200000000;
//             m_encoderSettings.fname = outfile;
//             m_encoderSettings.inputAlpha = false;

//             IRE_LOG_INFO("Creating system VIDEO_ENCODER");
//         }

//         virtual ~System() = default;

//         virtual void start() override {
//             m_dtvEncoder.run(m_encoderSettings, 20);

//         }

//         virtual void end() override {
//             IRE_LOG_INFO("Finalizing video encoding");
//             m_dtvEncoder.commit();
//             m_dtvEncoder.stop();
//         }


//         void tickWithArchetype(
//             Archetype type,
//             std::vector<EntityId>& entities
//         )
//         {

//         }

//     private:
//         const ivec2 m_inOutResolution;
//         const GLenum m_GLFormat;
//         const GLenum m_GLType;
//         std::vector<uint8_t> m_outbuffer;
//         // std::vector<uint8_t> m_flippedPixels;
//         atg_dtv::Encoder m_dtvEncoder;
//         atg_dtv::Encoder::VideoSettings m_encoderSettings;

//         virtual void beginExecute() override {
//             atg_dtv::Frame* newFrame = m_dtvEncoder.newFrame(false);
//             if(m_dtvEncoder.getError() != atg_dtv::Encoder::Error::None) {
//                 IRE_LOG_ERROR("Encoder error: {}", (int)m_dtvEncoder.getError());
//                 IR_ASSERT(false, "Encoder error");
//             }
//             ENG_API->glReadBuffer(GL_FRONT);
//             ENG_API->glReadPixels(
//                 0,
//                 0,
//                 m_inOutResolution.x,
//                 m_inOutResolution.y,
//                 m_GLFormat,
//                 m_GLType,
//                 m_outbuffer.data()
//             );

//             // int rowSize = m_inOutResolution.x * 3;

//             // for(int i = 0; i < m_inOutResolution.y; i++) {
//             //     memcpy(
//             //         m_flippedPixels.data() + (i * rowSize),
//             //         m_outbuffer.data() + ((m_inOutResolution.y - i - 1) * rowSize),
//             //         rowSize
//             //     );
//             // }

//             // for(int x = 0; x < m_inOutResolution.x; x++) {
//             //     for(int y = 0; y < m_inOutResolution.y; y++) {
//             //         int targetIndex = (x + y * m_inOutResolution.x) * 3;
//             //         newFrame->m_rgb[
//             //             (x + y * m_inOutResolution.x) * 3
//             //         ] = m_outbuffer[
//             //             (x + y * m_inOutResolution.x) * 3
//             //         ];
//             //         int index = (x + y * m_inOutResolution.x) * 3;
//             //         std::swap(
//             //             m_outbuffer[index],
//             //             m_outbuffer[index + 2]
//             //         );
//             //     }
//             // }
//             if(newFrame != nullptr) {
//                 newFrame->m_rgb = m_outbuffer.data();
//                 m_dtvEncoder.submitFrame();

//             }

//         }

//         virtual void endExecute() override {

//         }


//     };


// } // namespace IRECS


// #endif /* SYSTEM_VIDEO_ENCODER_H */
