"""Execute resource-free fog composition and the compute adapters on both backends."""

import re
import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function, replace_once
from test_render_source_face_lighting import COMPILER, PREAMBLE, SHADERS

ADAPTER = r"""
#include <cstdio>
float dot(vec3 a,vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
vec3 mix(vec3 a,vec3 b,float t){return a*(1.f-t)+b*t;}
float smoothstep(float lo,float hi,float x){
    const float t=std::clamp((x-lo)/(hi-lo),0.f,1.f);
    return t*t*(3.f-2.f*t);
}
struct FogObserverData {
    vec4 unexploredColor;
    int visionCircleCount;
    vec4 revealSurfaceTreatment;
};
vec4 unexploredColor(0,0,0,0),revealSurfaceTreatment(0,0,0,0);
int visionCircleCount=0;
"""

CASES = r"""
double blend(double a,double b,double t){return a*(1.-t)+b*t;}
double smooth(double lo,double hi,double x){
    const double t=std::clamp((x-lo)/(hi-lo),0.,1.);
    return t*t*(3.-2.*t);
}
vec4 expectedColor(FogReveal r,int axis,vec4 src,FogObserverData obs,bool body){
    const double color[]={src.x,src.y,src.z};
    const double hidden[]={obs.unexploredColor.x,obs.unexploredColor.y,obs.unexploredColor.z};
    const double gray=(color[0]*.299+color[1]*.587+color[2]*.114)*.4;
    double out[3];
    const double explored=128./255.;
    for(int i=0;i<3;++i){
        out[i]=r.state>=explored ? blend(gray,color[i],(r.state-explored)/(1.-explored))
                                : blend(hidden[i],gray,r.state/explored);
        if(body)continue;
        if(r.gridState<explored){
            const double u=1.-smooth(0,8,r.hardDistPastRim);
            out[i]=blend(out[i],color[i],.75*u*u);
            if(obs.visionCircleCount>0 && axis!=2){
                const double cap=1.-smooth(0,2,std::max(double(r.hardDistPastRim),0.));
                out[i]=blend(out[i],color[i]*(.85+.15*r.state),cap);
            }
        }
        if(r.styledBand>0)
            out[i]=blend(out[i],color[i]*blend(obs.revealSurfaceTreatment.z,1.,r.state),r.styledBand);
    }
    return vec4(float(out[0]),float(out[1]),float(out[2]),src.a);
}
int main(){
    int count=0;
    for(vec4 src:{vec4(0,0,0,0),vec4(.13f,.57f,.91f,.37f),vec4(1,1,1,1)})
    for(vec4 hidden:{vec4(0,0,0,0),vec4(.9f,.1f,.3f,1)})
    for(float state:{0.f,.25f,128.f/255.f,.75f,.999f})
    for(float grid:{0.f,128.f/255.f,1.f})for(int face=0;face<6;++face)
    for(float rim:{-1.f,0.f,1.f,2.f,4.f,8.f,12.f})for(int circles:{0,1,8})
    for(float band:{0.f,.3f,1.f})for(float tone:{.2f,.85f,1.f}){
        const FogReveal reveal{state,grid,rim,band};
        const FogObserverData obs{hidden,circles,vec4(1,.5f,tone,17)};
        unexploredColor=hidden;visionCircleCount=circles;revealSurfaceTreatment=obs.revealSurfaceTreatment;
        const vec4 surface=fogApplyReveal(
            reveal,face/2,src,hidden,circles,obs.revealSurfaceTreatment);
        const vec4 body=fogApplyBody(state,src,hidden);
        if(!same(surface,expectedColor(reveal,face/2,src,obs,false)))return 1;
        if(!same(body,expectedColor(reveal,face/2,src,obs,true)))return 2;
        if(!same(surface,computeReveal(reveal,face/2,src,obs)))return 3;
        if(!same(body,computeBody(state,src,obs)))return 4;
        ++count;
    }
    for(float visibility:{0.f,.125f,.5f,.875f,1.f}){
        const float band=fogRevealSurfaceBandWeight(visibility);
        if(!near(band,4*visibility*(1-visibility)))return 5;
        if(!near(band,fogRevealSurfaceBandWeight(1-visibility)))return 5;
    }
    std::printf("%d fog composition cases\n",count);
}
"""


def host(source):
    source = re.sub(r'^#include "[^"\n]+"\s*$', '', source, flags=re.MULTILINE)
    source = re.sub(r'\bconstant\s+FogObserverData&', 'const FogObserverData&', source)
    return (source.replace("constant ", "const ").replace("float3", "vec3")
            .replace("float4", "vec4").replace(".rgb", ".rgb()"))


def harness(suffix, directory, mutation):
    base = SHADERS / directory
    helper = (base / f"ir_fog_apply.{suffix}").read_text()
    common = (base / f"ir_fog_common.{suffix}").read_text()
    color = (base / f"ir_fog_color.{suffix}").read_text()
    wrapper = (extract_function(common, "fogApplyReveal") + "\n"
               + extract_function(common, "fogApplyBody"))
    helper, wrapper = host(helper), host(wrapper)
    if mutation == "top-cap":
        helper = replace_once(helper, "faceAxis != 2", "faceAxis == 2")
    elif mutation == "explored-rim":
        helper = replace_once(helper, "reveal.gridState < kFogExploredValue", "true")
    elif mutation == "linear-rim":
        helper = replace_once(helper, "kFogRimFadeLevel * u * u", "kFogRimFadeLevel * u")
    elif mutation == "lost-alpha":
        helper = helper.replace("sourceColor.a", "1.0")
    elif mutation == "lost-band":
        helper = replace_once(helper, "if (reveal.styledBand >", "if (0.0 >")
    elif mutation == "wrong-adapter-tone":
        prefix = "" if suffix == "glsl" else "fogObservers."
        wrapper = replace_once(wrapper, prefix + "visionCircleCount,", "0,")
    elif mutation == "wrong-adapter-anchor":
        prefix = "" if suffix == "glsl" else "fogObservers."
        wrapper = replace_once(wrapper, "sourceColor, " + prefix + "unexploredColor)",
                               "sourceColor, sourceColor)")
    if suffix == "glsl":
        adapters = """
vec4 computeReveal(FogReveal r,int axis,vec4 src,FogObserverData obs){
    return fogApplyReveal(r,axis,src);
}
vec4 computeBody(float state,vec4 src,FogObserverData obs){return fogApplyBody(state,src);}
"""
    else:
        adapters = """
vec4 computeReveal(FogReveal r,int axis,vec4 src,FogObserverData obs){
    return fogApplyReveal(r,axis,src,obs);
}
vec4 computeBody(float state,vec4 src,FogObserverData obs){return fogApplyBody(state,src,obs);}
"""
    return PREAMBLE + ADAPTER + host(color) + helper + wrapper + adapters + CASES


@unittest.skipUnless(COMPILER, "fog composition controls require a C++ compiler")
class FogCompositionTests(unittest.TestCase):
    def test_composition_and_adapters_reject_mutations(self):
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            original = harness(suffix, directory, "production")
            for mutation in ("production", "top-cap", "explored-rim", "linear-rim", "lost-alpha",
                             "lost-band", "wrong-adapter-tone", "wrong-adapter-anchor"):
                with self.subTest(backend=suffix, mutation=mutation), \
                        tempfile.TemporaryDirectory() as tmp:
                    source = harness(suffix, directory, mutation)
                    if mutation != "production":
                        self.assertNotEqual(source, original)
                    path = Path(tmp)
                    (path / "fog.cpp").write_text(source)
                    build = subprocess.run(
                        [COMPILER, "-std=c++17", str(path / "fog.cpp"), "-o", str(path / "fog")],
                        capture_output=True, text=True)
                    self.assertEqual(build.returncode, 0, build.stderr)
                    run = subprocess.run([str(path / "fog")], capture_output=True, text=True)
                    if mutation == "production":
                        self.assertEqual(run.returncode, 0, run.stderr)
                        self.assertIn("102060 fog composition cases", run.stdout)
                    else:
                        self.assertIn(run.returncode, (1, 2, 3, 4, 5), run.stderr)


if __name__ == "__main__":
    unittest.main()
