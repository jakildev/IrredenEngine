"""Execute finite receiver queries, including producer placement and fallback contracts."""

import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function
from test_render_sdf_surface_contract import CASES

ROOT = Path(__file__).resolve().parents[2]
COMPILER = shutil.which("c++")
PREAMBLE = r"""
#include <algorithm>
#include <cmath>
#include <cstdio>
using std::abs; using std::min; using std::max;
using uint = unsigned;
template<class T> struct V2 {
 T x,y; V2():x(0),y(0){} V2(T a,T b):x(a),y(b){}
 template<class U> explicit V2(V2<U> v):x(T(v.x)),y(T(v.y)){}
 V2 operator+(V2 b)const{return {x+b.x,y+b.y};}
 V2 operator-(V2 b)const{return {x-b.x,y-b.y};}
 V2 operator*(T b)const{return {x*b,y*b};}
};
template<class T> struct V3 {
 T x,y,z; V3():x(0),y(0),z(0){} explicit V3(T a):x(a),y(a),z(a){}
 V3(T a,T b,T c):x(a),y(b),z(c){}
 template<class U> explicit V3(V3<U> v):x(T(v.x)),y(T(v.y)),z(T(v.z)){}
 V3 operator+(V3 b)const{return {x+b.x,y+b.y,z+b.z};}
 V3 operator-(V3 b)const{return {x-b.x,y-b.y,z-b.z};}
 V3 operator-(T b)const{return {x-b,y-b,z-b};}
 V3 operator*(T b)const{return {x*b,y*b,z*b};}
 V3 operator*(V3 b)const{return {x*b.x,y*b.y,z*b.z};}
 V3 operator/(T b)const{return {x/b,y/b,z/b};}
 V3 operator/(V3 b)const{return {x/b.x,y/b.y,z/b.z};}
 V3 operator-()const{return {-x,-y,-z};}
};
using vec2=V2<float>; using ivec2=V2<int>; using vec3=V3<float>; using ivec3=V3<int>;
vec3 abs(vec3 v){return {abs(v.x),abs(v.y),abs(v.z)};}
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 clamp(vec3 v,vec3 lo,vec3 hi){return {
 std::clamp(v.x,lo.x,hi.x),std::clamp(v.y,lo.y,hi.y),std::clamp(v.z,lo.z,hi.z)};}
struct vec4 {vec3 xyz; float w=1;};
struct ShapeDescriptor {vec4 worldPosition,params,rotation; uint shapeType=0,flags=0;};
struct ShapeProjectionData {
 ivec2 voxelRenderOptions,trixelCanvasOffsetZ1; vec2 frameCanvasOffset;
 int smoothYawEnabled=0,latticeShapes=0; float visualYaw=0,rasterYaw=0,residualYaw=0;
};
const uint SHAPE_BOX=0;
ivec3 roundHalfUp(vec3 v){return {int(floor(v.x+.5f)),int(floor(v.y+.5f)),int(floor(v.z+.5f))};}
ivec2 roundHalfUp(vec2 v){return {int(floor(v.x+.5f)),int(floor(v.y+.5f))};}
ivec2 pos3DtoPos2DIso(ivec3 v){return {-v.x+v.y,-v.x-v.y+2*v.z};}
ivec2 trixelFrameOffset(ivec2 origin,vec2 camera,ivec2 options){
 int d=options.x ? max(options.y,1):1;
 return origin+ivec2(int(floor(camera.x*d)),int(floor(camera.y*d)));
}
"""
CHECKS = r"""
int main(){
 int hits=0,misses=0,recoveredHits=0;
 for(int smooth:{0,1}) for(int density:{1,2,4,8})
 for(int step=0;step<32;++step) for(float phase:{-.37f,.19f}){
  ShapeProjectionData f;
  f.smoothYawEnabled=smooth; f.voxelRenderOptions={1,density};
  f.visualYaw=float(step*acos(-1.0)/16.0);
  f.rasterYaw=float((step/8)*acos(-1.0)/2.0);
  f.trixelCanvasOffsetZ1={51,83}; f.frameCanvasOffset={phase*3,phase*7};
  ShapeDescriptor shape; shape.worldPosition.xyz={phase*13,phase*7,phase*5};
  shape.params.xyz={4.3f,6.7f,3.1f};
  const double yaw=smooth?f.visualYaw:f.rasterYaw;
  const double c=cos(yaw),s=sin(yaw);
  const auto center=shape.worldPosition.xyz;
  double cx=center.x,cy=center.y,cz=center.z;
  double vx=c*cx+s*cy,vy=-s*cx+c*cy;
  double ox,oy;
  if(smooth){ox=floor((-vx+vy)*density+.5);oy=floor((-vx-vy+2*cz)*density+.5);}
  else{
   vx=floor(vx+.5);vy=floor(vy+.5);cz=floor(cz+.5);
   cx=c*vx-s*vy;cy=s*vx+c*vy;
   ox=(-vx+vy)*density;oy=(-vx-vy+2*cz)*density;
  }
  for(int x=-18;x<=18;++x) for(int y=-18;y<=18;++y){
   vec2 pixel{float(51+floor(f.frameCanvasOffset.x*density)+ox+x+.25),
              float(83+floor(f.frameCanvasOffset.y*density)+oy+y-.125)};
   vec3 position{999},normal{999};
   bool actual=shapeBoxReceiver(shape,f,pixel,false,position,normal);
   vec3 recoveredPosition{999},recoveredNormal{999};
   bool recovered=shapeBoxReceiver(shape,f,pixel,true,recoveredPosition,recoveredNormal);
   if(!smooth && density==1){if(actual||recovered)return 1;continue;}
   double ix=x+.25,iy=y-.125;
   double vx0=-ix/2-iy/6,vy0=ix/2-iy/6;
   double origin[]={c*vx0-s*vy0,s*vx0+c*vy0,iy/3};
   double direction[]={(c-s)/3,(s+c)/3,1.0/3};
   double half[]={(shape.params.xyz.x-1)*density*.5+.5,
                  (shape.params.xyz.y-1)*density*.5+.5,
                  (shape.params.xyz.z-1)*density*.5+.5};
   Hit expected=intersect(origin,direction,half);
   if(actual!=expected.valid||!recovered)return 2;
   if(!actual){
    ++misses;++recoveredHits;
    float p[]={recoveredPosition.x,recoveredPosition.y,recoveredPosition.z};
    float n[]={recoveredNormal.x,recoveredNormal.y,recoveredNormal.z};
    double ctr[]={cx,cy,cz};
    int boundaries=0,normalAxes=0;
    for(int a=0;a<3;++a){
     double local=(p[a]-ctr[a])*density;
     if(abs(local)>half[a]+1.e-4)return 12;
     boundaries+=abs(abs(local)-half[a])<1.e-4;
     normalAxes+=abs(n[a])==1.f;
     if(n[a]!=-1.f&&n[a]!=0.f&&n[a]!=1.f)return 13;
     if(n[a]!=0.f&&abs(local-n[a]*half[a])>1.e-4)return 19;
    }
    if(!boundaries||normalAxes!=1)return 14;
    continue;
   }
   ++hits;
   float p[]={position.x,position.y,position.z},n[]={normal.x,normal.y,normal.z};
   float rp[]={recoveredPosition.x,recoveredPosition.y,recoveredPosition.z};
   float rn[]={recoveredNormal.x,recoveredNormal.y,recoveredNormal.z};
   double ctr[]={cx,cy,cz};
   for(int a=0;a<3;++a){
    double wanted=ctr[a]+(origin[a]+direction[a]*expected.entry)/density;
    if(abs(p[a]-wanted)>1.e-4)return 3;
    double wn=expected.face/2==a?(expected.face%2?1:-1):0;
    if(n[a]!=wn)return 4;
    if(rp[a]!=p[a]||rn[a]!=n[a])return 15;
   }
  }
 }
 ShapeProjectionData f;f.voxelRenderOptions={1,3};f.smoothYawEnabled=1;
 ShapeDescriptor shape;shape.params.xyz={5,5,5};vec3 p,n;
 if(!shapeBoxReceiver(shape,f,{0,0},false,p,n))return 5;
 shape.flags=1;if(shapeBoxReceiver(shape,f,{0,0},true,p,n))return 6;shape.flags=0;
 shape.shapeType=1;if(shapeBoxReceiver(shape,f,{0,0},true,p,n))return 7;shape.shapeType=0;
 shape.rotation.w=.5;if(shapeBoxReceiver(shape,f,{0,0},true,p,n))return 8;shape.rotation.w=1;
 f.voxelRenderOptions={1,1};f.latticeShapes=1;
 if(shapeBoxReceiver(shape,f,{0,0},true,p,n))return 9;
 f.voxelRenderOptions={1,3};f.smoothYawEnabled=0;f.residualYaw=.1;
 if(shapeBoxReceiver(shape,f,{0,0},true,p,n))return 10;
 if(!hits||!misses||recoveredHits!=misses)return 11;
 int nearbyMisses=0;double maxRecoveryShift=0;
 for(vec3 dimensions:{vec3(64,1,1),vec3(1,1,32)}) for(int density:{1,4})
 for(int step=0;step<32;++step){
  f={};f.smoothYawEnabled=1;f.voxelRenderOptions={1,density};
  f.visualYaw=float(step*acos(-1.0)/16.0);
  shape={};shape.params.xyz=dimensions;
  const double c=cos(f.visualYaw),s=sin(f.visualYaw);
  double half[]={(dimensions.x-1)*density*.5+.5,
                 (dimensions.y-1)*density*.5+.5,
                 (dimensions.z-1)*density*.5+.5};
  double direction[]={(c-s)/3,(s+c)/3,1.0/3};
  auto query=[&](double ix,double iy){
   double vx=-ix/2-iy/6,vy=ix/2-iy/6;
   double origin[]={c*vx-s*vy,s*vx+c*vy,iy/3};
   return intersect(origin,direction,half).valid;
  };
  int radius=int(2*(half[0]+half[1]+half[2]))+4;
  for(int x=-radius;x<=radius;++x)for(int y=-radius;y<=radius;++y){
   double ix=x+.25,iy=y-.125;
   if(query(ix,iy))continue;
   bool withinDilation=false;
   for(int dx=0;dx<=2;++dx)for(int dy=0;dy<=3;++dy)
    withinDilation|=(dx||dy)&&query(ix-dx,iy-dy);
   if(!withinDilation)continue;
   vec3 recoveredPosition,recoveredNormal;
   if(!shapeBoxReceiver(shape,f,{float(ix),float(iy)},true,
                        recoveredPosition,recoveredNormal))return 16;
   double viewX=c*recoveredPosition.x*density+s*recoveredPosition.y*density;
   double viewY=-s*recoveredPosition.x*density+c*recoveredPosition.y*density;
   double recoveredX=-viewX+viewY;
   double recoveredY=-viewX-viewY+2*recoveredPosition.z*density;
   double shift=hypot(recoveredX-ix,recoveredY-iy);
   if(shift>5.0)return 17;
   maxRecoveryShift=max(maxRecoveryShift,shift);++nearbyMisses;
  }
 }
 if(!nearbyMisses)return 18;
 // A splatted floor silhouette retains coverage outside the finite ray hit.
 // These local iso queries straddle its +X/-Y corner at two settled yaws.
 int floorCorners=0;
 for(int pose=0;pose<2;++pose){
  f={};f.smoothYawEnabled=1;f.voxelRenderOptions={1,1};
  f.visualYaw=pose==0?1.308994f:1.0471947f;
  shape={};shape.params.xyz={120,120,4};shape.worldPosition.xyz={0,0,4};
  for(float dx:{0.f,.5f})for(float dy:{0.f,1.f}){
   const vec2 relative{(pose==0?-30.75f:-59.75f)+dx,
                       (pose==0?120.5f:108.5f)+dy};
   const vec2 pixel=relative+vec2(0,8);
   vec3 position,normal;
   if(shapeBoxReceiver(shape,f,pixel,false,position,normal))return 20;
   if(!shapeBoxReceiver(shape,f,pixel,true,position,normal))return 21;
   if(normal.x!=0||normal.y!=-1||normal.z!=0)return 22;
   if(abs(position.y+60)>1.e-4||position.x<59.2f||position.x>59.8f||
      position.z<5.99f||position.z>6.0001f)return 23;
   // The recovered point's independently intersected ray must enter -Y,
   // rather than merely report an arbitrary axis normal on some box boundary.
   const double c=cos(double(f.visualYaw)),s=sin(double(f.visualYaw));
   const double vx=c*position.x+s*position.y,vy=-s*position.x+c*position.y;
   const double ix=-vx+vy,iy=-vx-vy+2*(position.z-4);
   double origin[]={-c*ix/2-c*iy/6-s*ix/2+s*iy/6,
                    -s*ix/2-s*iy/6+c*ix/2-c*iy/6,iy/3};
   double direction[]={(c-s)/3,(s+c)/3,1./3},half[]={60,60,2};
   Hit expected=intersect(origin,direction,half);
   if(!expected.valid||expected.face!=2)return 24;
   ++floorCorners;
  }
 }
 f={};f.smoothYawEnabled=1;f.voxelRenderOptions={1,4};f.visualYaw=.35f;
 shape={};shape.params.xyz={4,4,1};shape.worldPosition.xyz={40,0,2};
 const ivec2 panelOrigin=roundHalfUp(pos3DtoPos2DIsoYawed(
     shape.worldPosition.xyz*4.f,f.visualYaw));
 for(vec2 relative:{vec2(13,-5),vec2(8,6),vec2(7,8)}){
  vec3 position,normal;
  if(shapeBoxReceiver(shape,f,vec2(panelOrigin)+relative,false,position,normal))return 25;
 }
 int emitterHits=0,emitterMisses=0;
 for(int cardinal=0;cardinal<4;++cardinal)for(int x=-20;x<=20;++x)
 for(int y=-20;y<=20;++y){
  float yaw=.35f+float(cardinal*acos(-1.)/2),c=cos(yaw),s=sin(yaw);
  const vec3 half(6,6,0);const ivec2 iso(x,y);
  float entry,exitDepth;vec3 normal;
  bool hit=boxSurfaceIntervalYaw(float(x),float(y),half+vec3(.5f),c,s,entry,exitDepth,normal);
  int depth=boxDepthIntersectYaw(iso,half,false,c,s);
  bool emitted=emitterPublishesDepth(depth);
  if(!hit){++emitterMisses;if(emitted)return 26;}
  if(emitted){++emitterHits;if(shapeBoxFaceSlot(iso,half,c,s,cardinal)<0)return 27;}
 }
 if(!emitterHits||!emitterMisses)return 28;
 printf("receiver_hits=%d misses=%d recovered_hits=%d nearby_misses=%d max_shift=%.3f\n",
        hits,misses,recoveredHits,nearbyMisses,maxRecoveryShift);
 printf("floor_corner_receivers=%d signed_normal=-Y\n",floorCorners);return 0;
}
"""


@unittest.skipUnless(COMPILER, "receiver controls require a C++ compiler")
class ShapeReceiverTest(unittest.TestCase):
    def test_receiver_face_unorm_roundtrip(self):
        harness = r"""
#include <cmath>
struct vec3 {
 float x,y,z;
 float operator[](int i)const{return i==0?x:(i==1?y:z);}
};
vec3 faceOutwardNormal6(int face){
 vec3 n{0,0,0};float s=(face&1)?1.f:-1.f;
 if(face/2==0)n.x=s;else if(face/2==1)n.y=s;else n.z=s;
 return n;
}
bool same(vec3 a,vec3 b){return a.x==b.x&&a.y==b.y&&a.z==b.z;}
"""
        main = r"""
int main(){
 vec3 fallback{0.3f,-0.4f,0.5f};
 if(!same(applyReceiverFace(0.f,fallback),fallback))return 1;
 for(int face=0;face<6;++face){
  vec3 n=faceOutwardNormal6(face);
  int byte=int(std::round(encodeReceiverFace(n)*255.f));
  if(byte!=face+1)return 2;
  if(!same(applyReceiverFace(float(byte)/255.f,fallback),n))return 3;
 }
 return 0;
}
"""
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shader = ROOT / "engine/render/src/shaders" / folder / f"ir_receiver_face.{suffix}"
            source = shader.read_text().replace("float3", "vec3")
            lighting = (shader.parent / f"c_lighting_to_trixel_body.{suffix}").read_text()
            override = re.search(r"    worldNormal = receiverFaceNormal\([^;]+;", lighting).group()
            texture_read = ("imageLoad(canvasSunShadow, pixel).a" if suffix == "glsl"
                            else "canvasSunShadow.read(uint2(pixel)).a")
            override = override.replace(texture_read, "encoded")
            consumer = ("vec3 applyReceiverFace(float encoded,vec3 worldNormal){\n"
                        + override + "\nreturn worldNormal;}\n")
            for mutation in ("none", "lost_sentinel", "lost_consumer"):
                candidate = source + consumer
                if mutation == "lost_sentinel":
                    candidate = candidate.replace("float(face + 1)", "float(face)")
                if mutation == "lost_consumer":
                    candidate = candidate.replace(override, "")
                with tempfile.TemporaryDirectory() as tmp:
                    cpp, exe = Path(tmp) / "face.cpp", Path(tmp) / "face"
                    cpp.write_text(harness + candidate + main)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    self.assertEqual(run.returncode == 0, mutation == "none")

    def test_plain_kernel_excludes_receiver_code(self):
        def expand(path):
            source = path.read_text()
            source = re.sub(r'^#include "([^"\n]+)"',
                            lambda m: expand(path.parent / m.group(1)), source, flags=re.M)
            return re.sub(r"^#(?:include <.*>|version .*).*", "", source, flags=re.M)

        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            kernels = (
                ("c_compute_sun_shadow", ("shapeBoxReceiver", "receiverShapes",
                                          "receiverOwners", "receiverTiles", "receiverFrame")),
                ("c_lighting_to_trixel", ("receiverFaceNormal",)),
                ("f_trixel_to_framebuffer" if suffix == "glsl" else "trixel_to_framebuffer",
                 ("selectedShapeBoxReceiver", "receiverShapes", "receiverOwners",
                  "receiverTiles", "receiverFrame", "worldSunShadowFactor")),
            )
            for kernel, tokens in kernels:
                for variant, enabled in (("", False), ("_shapes", True)):
                    source = expand(shaders / f"{kernel}{variant}.{suffix}")
                    for mutation in (False, True):
                        candidate = source
                        if mutation:
                            candidate = source.replace(
                                f"#define IR_SHAPE_RECEIVER {int(enabled)}",
                                f"#define IR_SHAPE_RECEIVER {int(not enabled)}")
                            self.assertNotEqual(candidate, source)
                        result = subprocess.run([COMPILER, "-E", "-P", "-x", "c++", "-"],
                                                input=candidate, text=True, capture_output=True)
                        self.assertEqual(result.returncode, 0, result.stderr)
                        contains_receiver = enabled != mutation
                        for token in tokens:
                            self.assertEqual(token in result.stdout, contains_receiver,
                                             (suffix, variant, mutation, token))

    def test_consumer_selects_elected_descriptor(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            shader = (shaders / f"c_compute_sun_shadow_body.{suffix}").read_text()
            start = shader.index("    if (selectedShapeBoxReceiver(")
            block = shader[start:shader.index("#endif", start)]
            per_axis = re.search(
                r"    if \((?:frameData\.)?perAxisRoute != 0\) \{\s*const int faceId\b.*?\n    \}",
                shader, re.DOTALL)
            self.assertIsNotNone(per_axis)
            self.assertLess(per_axis.end(), start)
            block = per_axis[0] + "\n" + block
            block = block.replace("frameData.", "").replace("sunFrameData.", "")
            helper = (shaders / f"ir_selected_shape_receiver.{suffix}").read_text()
            helper = helper[helper.index("int selectedShapeIndex"):]
            helper = helper.replace("inout vec3 ", "vec3& ")
            helper = helper.replace("thread float3 &", "vec3& ")
            helper = helper.replace("constant ShapeProjectionData &receiverFrame,", "")
            helper = helper.replace("device const ShapeDescriptor *receiverShapes,", "")
            helper = helper.replace("device const uint *receiverOwners,", "")
            helper = helper.replace("device const ShapeTileDescriptor *receiverTiles,", "")
            helper = helper.replace("device const ShapeTileDescriptor *receiverTiles)", ")")
            helper = re.sub(r",\s*\)", ")", helper)
            helper = re.sub(r",\s*receiverFrame, receiverOwners, receiverTiles\)",
                            ")", helper)
            block = block.replace(
                "receiverFrame, receiverShapes, receiverOwners, receiverTiles,", "")
            block = re.sub(
                r",\s*receiverFrame, receiverShapes, receiverOwners, receiverTiles\)",
                ")", block)
            for source, target in (("float4", "vec4"), ("float3", "vec3"),
                                   ("float2", "vec2"), ("ivec2", "Point"),
                                   ("uint2", "Point"), ("int2", "Point")):
                helper = helper.replace(source, target)
                block = block.replace(source, target)
            block = helper + "\nvoid run(){\n" + block + "}\n"
            data = (shaders / f"ir_shape_data.{suffix}").read_text()
            stride = re.search(r"(?:const|constant) uint kShapeSamplesPerTile = [^;]+;",
                               data).group().replace("constant", "const")
            harness = r"""
#include <vector>
#include <cstdlib>
#include <cmath>
#include <algorithm>
using std::abs;using std::max;
using uint=unsigned;
struct vec2 {float x,y; vec2(float a,float b):x(a),y(b){}
 template<class T> vec2(T v):x(v.x),y(v.y){} };
struct vec3 {int value=0;};
struct vec4 {vec4(float,float,float,float){}};
struct Tile {int shapeIndex;};
struct Frame {int shapeCount;struct {int x=1,y=1;} voxelRenderOptions;
 int smoothYawEnabled=0,latticeShapes=0;};
const uint SHAPE_BOX=0;
struct ShapeDescriptor {int value;uint shapeType=1,flags=0;
 struct {float w=1;} rotation;operator int()const{return value;}};
struct Point {int x,y;};
struct Canvas {void write(vec4,Point){}} canvasSunShadow;
void imageStore(Canvas&,Point,vec4){}
int perAxisRoute=0,perAxisSamples=0,face=0,flip=0,encoded=0,shadowsEnabled=1;
int visibleFaceIds[3]={0,2,4},sunFrameData=0,sunDepthBuf=0,perAxisStoreFrame=0;
vec3 perAxisCellToWorld3DSubCell(Point,int,int,int){return {};}
float perAxisSunShadowFactor(vec3,int,int=0,int=0){++perAxisSamples;return 1;}
template<class T> struct Checked {
 std::vector<T> data;int reads=0;
 T operator[](size_t i){++reads;if(i>=data.size())std::exit(42);return data[i];}
};
Frame receiverFrame{3}; Point pixel{1,1},size{3,2};
Checked<uint> receiverOwners{{0xffffffffu,0xffffffffu,0xffffffffu,
                              0xffffffffu,0xffffffffu,0xffffffffu}};
Checked<Tile> receiverTiles{{{0},{2}}};
Checked<ShapeDescriptor> receiverShapes{{{11},{22},{33}}};
vec3 pos3D{7},normal{8}; bool finiteHit=true;
vec3 faceOutwardNormal6(int face){return {100+face};}
float receiverFace=0;
float encodeReceiverFace(vec3 n){return float(n.value);}
vec2 lastQuery{0,0};bool lastRecover=false;
bool shapeBoxReceiver(int shape,Frame,vec2 query,bool recover,vec3& p,vec3& n){
 lastQuery=query;lastRecover=recover;p.value=shape;n.value=-shape;return finiteHit;
}
"""
            main = r"""
int main(){
 for(uint local=0;local<64;++local)for(uint face=0;face<3;++face)
 for(uint half=0;half<2;++half){
  receiverOwners.data[4]=((1u*64u+local)*3u+face)*2u+half;
  run();if(lastRecover||pos3D.value!=33||normal.value!=-33||receiverFace!=-33)return 1;
 }
 for(int kind=0;kind<4;++kind){
  perAxisRoute=kind==0;receiverFrame.shapeCount=kind==1?0:3;
  receiverOwners.data[4]=kind==2?0xffffffffu:384u;finiteHit=kind!=3;
  pos3D.value=7;normal.value=8;receiverFace=0;
  receiverOwners.reads=receiverTiles.reads=receiverShapes.reads=perAxisSamples=0;run();
  if(perAxisSamples!=(kind==0))return 2;
  if(pos3D.value!=7||normal.value!=8||receiverFace!=0)return 2;
  if(kind<2&&receiverOwners.reads!=0)return 3;
  if(kind<3&&(receiverTiles.reads!=0||receiverShapes.reads!=0))return 4;
 }
 perAxisRoute=0;receiverFrame.shapeCount=3;finiteHit=true;
 receiverOwners.data[4]=384u;
 vec3 p{7},n{8};
 if(!selectedShapeBoxReceiver(pixel,size.x,vec2(1.25f,-.75f),false,p,n))return 5;
 if(lastQuery.x!=1.25f||lastQuery.y!=-.75f||lastRecover||p.value!=33||n.value!=-33)return 5;
 finiteHit=false;p.value=7;n.value=8;
 if(selectedShapeBoxReceiver(pixel,size.x,vec2(100.f,100.f),false,p,n))return 6;
 if(p.value!=7||n.value!=8)return 6;
 receiverFrame.smoothYawEnabled=1;receiverShapes.data[2].shapeType=SHAPE_BOX;
 for(int cardinal=0;cardinal<4;++cardinal)for(int slot=0;slot<3;++slot){
  const int triplets[4][3]={{0,2,4},{2,1,4},{1,3,4},{3,0,4}};
  for(int i=0;i<3;++i)visibleFaceIds[i]=triplets[cardinal][i];
  face=slot;pos3D.value=7;normal.value=8;receiverFace=0;run();
  if(pos3D.value!=7||normal.value!=100+triplets[cardinal][slot]||
     receiverFace!=normal.value||lastRecover)return 7;
 }
 for(int kind=0;kind<6;++kind){
  receiverFrame.smoothYawEnabled=kind==0?0:1;
  receiverFrame.latticeShapes=kind==1||kind==5?1:0;
  receiverFrame.voxelRenderOptions.y=kind==5?4:1;
  receiverShapes.data[2].shapeType=kind==2?1:SHAPE_BOX;
  receiverShapes.data[2].flags=kind==3?1u:0u;
  receiverShapes.data[2].rotation.w=kind==4?.5f:1.f;
  pos3D.value=7;normal.value=8;receiverFace=0;run();
  if(pos3D.value!=7||lastRecover)return 8;
  if(kind<5&&(normal.value!=8||receiverFace!=0))return 8;
  if(kind==5&&(normal.value!=104||receiverFace!=104))return 9;
 }
 for(int smooth:{0,1})for(int mode:{0,1})for(int density:{0,1,4})
 for(int lattice:{0,1})for(uint hollow:{0u,1u})for(uint type:{0u,1u})
 for(float rotation:{.5f,.9999f,1.f}){
  receiverFrame.smoothYawEnabled=smooth;receiverFrame.voxelRenderOptions={mode,density};
  receiverFrame.latticeShapes=lattice;receiverShapes.data[2].flags=hollow;
  receiverShapes.data[2].shapeType=type;receiverShapes.data[2].rotation.w=rotation;
  if(selectedShapeBoxUsesWorldFaceSlot(pixel,size.x)!=
     emitterUsesWorldFaceSlot(receiverShapes.data[2],receiverFrame))return 10;
 }
 return 0;
}
"""
            producer = (shaders / f"c_shapes_to_trixel_body.{suffix}").read_text()
            statements = []
            for name in ("smoothYaw", "renderMode", "subdivisions", "smoothMode",
                         "latticeWalk", "hasEntityRotation"):
                statement = re.search(r"(?:const )?(?:bool|int) " + name + r" = [^;]+;",
                                      producer).group()
                statement = statement.replace("frameData.", "")
                for field in ("smoothYawEnabled", "voxelRenderOptions", "latticeShapes"):
                    statement = statement.replace(field, "receiverFrame." + field)
                statements.append(statement)
            statements.append(re.search(
                r"(?:const )?bool hollow = \(shape.flags & FLAG_HOLLOW\) != 0u;",
                producer).group())
            outer = re.search(r"if \((smoothYaw && !latticeWalk)\)", producer).group(1)
            inner = re.search(r"if \(([^\n]+)\) \{\s*boxFaceSlot = shapeBoxFaceSlot",
                              producer).group(1)
            harness += (
                "\nbool emitterUsesWorldFaceSlot(ShapeDescriptor shape,Frame receiverFrame){\n"
                "const uint FLAG_HOLLOW=1u;\n" + "\n".join(statements)
                + "\nreturn (" + outer + ") && (" + inner + ");\n}\n")
            encoded = re.search(r"const uint sampleOwner = ([^;]+);", producer).group(1)
            encoded = encoded.replace("tileIdx", "1u").replace("gl_LocalInvocationIndex", "local")
            encoded = encoded.replace("gl_LocalInvocationID.y", "(local / 8u)")
            encoded = encoded.replace("gl_LocalInvocationID.x", "(local % 8u)")
            encoded = encoded.replace("localId.y", "(local / 8u)")
            encoded = encoded.replace("localId.x", "(local % 8u)")
            encoded = encoded.replace("subPixel", "half")
            main = main.replace("((1u*64u+local)*3u+face)*2u+half", encoded)
            variants = {
                "production": block,
                "wrong_tile": block.replace("key / kShapeSamplesPerTile", "0u"),
                "lost_sentinel": block.replace("key == 0xffffffffu", "false"),
                "lost_normal_carrier": block.replace(
                    "receiverFace = encodeReceiverFace(normal);", ""),
                "lost_axis_gate": block.replace("perAxisRoute != 0", "false"),
                "lost_axis_return": block.replace("        return;", ""),
                "snapped_query": block.replace(
                    "queryPixel, recoverMiss, exactPosition",
                    "vec2(ownerPixel), recoverMiss, exactPosition"),
                "sun_recovery_enabled": re.sub(
                    r"(selectedShapeBoxReceiver\([^;]+?vec2\(pixel\),\s*)false,",
                    r"\1true,", block, count=1),
                "lost_finite_fallback": block.replace(
                    "if (!shapeBoxReceiver", "if (false && !shapeBoxReceiver"),
                "lost_box_slot_fallback": block.replace(
                    "else if (selectedShapeBoxUsesWorldFaceSlot",
                    "else if (false && selectedShapeBoxUsesWorldFaceSlot"),
                "world_slot_on_nonbox": block.replace("shape.shapeType == SHAPE_BOX", "true"),
                "world_slot_on_lattice": block.replace(
                    "(smoothMode || receiverFrame.latticeShapes == 0)", "true"),
                "world_slot_on_hollow": block.replace("(shape.flags & 1u) == 0u", "true"),
                "world_slot_on_rotated": block.replace("abs(shape.rotation.w) >= 0.9999", "true"),
                "world_slot_at_cardinal": block.replace(
                    "receiverFrame.smoothYawEnabled != 0", "true"),
            }
            for name, query in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    cpp, exe = Path(tmp) / "select.cpp", Path(tmp) / "select"
                    cpp.write_text(harness + stride + "\n" + query + main)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                    else:
                        self.assertNotEqual(query, block)
                        self.assertIn(run.returncode, (1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 42), name)

    def test_shared_projection_layout(self):
        shaders = ROOT / "engine/render/src/shaders"

        def fields(text):
            text = re.sub(r"//[^\n]*", "", text)
            return re.findall(r"(vec[234]|ivec[234]|float[234]?|int[234]?|uint) "
                              r"\w+(\[\d+\])?;", text)

        glsl = (shaders / "ir_shape_data.glsl").read_text()
        metal = (shaders / "metal/ir_shape_data.metal").read_text()
        metal = metal.replace("float4", "vec4").replace("float2", "vec2")
        metal = metal.replace("int2", "ivec2")
        for name in ("ShapeProjectionData", "ShapeDescriptor", "ShapeTileDescriptor"):
            pattern = r"struct " + name + r" \{(.*?)\};"
            self.assertEqual(fields(re.search(pattern, glsl, re.S).group(1)),
                             fields(re.search(pattern, metal, re.S).group(1)))
        body = (shaders / "c_shapes_to_trixel_body.glsl").read_text()
        producer = re.search(r"uniform ShapesFrameData \{(.*?)\};", body, re.S).group(1)
        receiver = re.search(r"struct ShapeProjectionData \{(.*?)\};", glsl, re.S).group(1)
        self.assertEqual(fields(producer), fields(receiver))
        self.assertNotEqual(fields(producer.replace("vec4 faceDeform", "vec2 faceDeform")),
                            fields(receiver))

    def test_finite_query_and_fallbacks(self):
        for suffix, folder in (("glsl", ""), ("metal", "metal/")):
            shaders = ROOT / "engine/render/src/shaders" / folder
            common = (shaders / f"ir_iso_common.{suffix}").read_text()
            sdf = (shaders / f"ir_sdf_common.{suffix}").read_text()
            producer = (shaders / f"c_shapes_to_trixel_body.{suffix}").read_text()
            functions = "\n".join(extract_function(common, name) for name in (
                "rasterYawCardinalIndex", "cardinalYawCosSin", "pos3DtoPos2DIsoYawed",
                "isoPositionToPos3D", "yawedIsoDistance", "isoPixelToPos3D", "isoToLocal3D"))
            functions += "\n" + "\n".join(extract_function(sdf, name) for name in (
                "slabFromLinear", "boxSurfaceIntervalYaw", "sdfBox"))
            constants = (shaders / f"ir_constants.{suffix}").read_text()
            for name, source in (("kInvalidDepth", constants), ("kCeilBiasEpsilon", producer),
                                 ("kSdfBiasEpsilon", producer)):
                functions += "\n" + re.search(
                    r"(?:const|constant) (?:int|float) " + name + r" = [^;]+;", source).group()
            functions += "\n" + "\n".join(extract_function(producer, name) for name in (
                "stableCeilToInt", "boxSlabIntersectYaw", "viewToLocalYaw",
                "boxDepthIntersectYaw", "shapeBoxFaceSlot"))
            miss_guard = re.search(
                r"if \(surfaceD == kInvalidDepth\)\s*(?:\{\s*return;\s*\}|return;)",
                producer).group()
            functions += ("\nbool emitterPublishesDepth(int surfaceD){\n"
                          + miss_guard.replace("return;", "return false;")
                          + "\nreturn true;\n}\n")
            functions += "\n" + (shaders / f"ir_shape_receiver.{suffix}").read_text()
            functions = re.sub(r"out (float|vec3) (\w+)", r"\1& \2", functions)
            functions = functions.replace("thread ", "").replace("float3", "vec3")
            functions = functions.replace("float2", "vec2").replace("int3", "ivec3")
            functions = functions.replace("int2", "ivec2")
            functions = functions.replace("constant ", "const ")
            variants = {
                "production": functions,
                "lost_cell_expansion": functions.replace("+ vec3(0.5)", "+ vec3(0.0)"),
                "rounded_query": functions.replace(
                    "vec2 relativeIso = canvasPixel",
                    "vec2 relativeIso = vec2(roundHalfUp(canvasPixel))"),
                "lost_density": functions.replace("viewOffset.z) / float(density)",
                                                 "viewOffset.z) / 1.0"),
                "lattice_accepted": functions.replace("projection.latticeShapes != 0", "false"),
                "center_anchor": re.sub(
                    r"vec2 anchorIso = vec2\([^;]+;",
                    "vec2 anchorIso = vec2(0.0, 0.0);", functions, count=1),
                "reversed_recovered_normal": functions.replace(
                    "relativeIso = recoveredIso;",
                    "relativeIso = recoveredIso; normal = -normal;"),
                "unrelated_recovered_normal": functions.replace(
                    "relativeIso = recoveredIso;",
                    "relativeIso = recoveredIso; normal = vec3(1.0, 0.0, 0.0);"),
                "emitter_slab_miss_writes": functions.replace("return kInvalidDepth;", "return 0;"),
                "emission_ignores_miss": functions.replace("surfaceD == kInvalidDepth", "false"),
            }
            for name, body in variants.items():
                with (
                    self.subTest(backend=suffix, variant=name),
                    tempfile.TemporaryDirectory() as tmp,
                ):
                    if name != "production":
                        self.assertNotEqual(body, functions)
                    cpp, exe = Path(tmp) / "receiver.cpp", Path(tmp) / "receiver"
                    cpp.write_text(PREAMBLE + body + CASES.split("int main()")[0] + CHECKS)
                    build = subprocess.run([COMPILER, "-std=c++17", str(cpp), "-o", str(exe)],
                                           capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(exe)], capture_output=True, text=True)
                    if name == "production":
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                        print(suffix, run.stdout.strip())
                    else:
                        self.assertIn(run.returncode,
                                      (2, 3, 4, 9, 12, 13, 14, 15, 16, 17, 18,
                                       19, 20, 21, 22, 23, 24, 25, 26, 27, 28), name)


if __name__ == "__main__":
    unittest.main()
