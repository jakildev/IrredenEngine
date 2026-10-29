"""Execute the surface-lit fog composition on both backends against independent arithmetic."""

import subprocess
import tempfile
import unittest
from pathlib import Path

from shader_contract_helpers import extract_function, replace_once
from test_render_fog_composition import ADAPTER, PREAMBLE, host
from test_render_source_face_lighting import COMPILER, SHADERS

# The reveal and the ceiling cut tap textures; the stubs return the case's
# values and count the reveal taps, so a case can require the reveal skipped
# for BODY and cut samples.
STUBS = r"""
namespace access { enum { read }; }
template <class T, int A> struct texture2d {};
FogReveal g_reveal{1,1,8,0};
bool g_cut=false;
int g_revealTaps=0;
FogReveal fogRevealSample(vec3,vec3,vec3,int,float,bool){++g_revealTaps;return g_reveal;}
FogReveal fogRevealSample(vec3,vec3,vec3,int,float,bool,const FogObserverData&,
    texture2d<uint,access::read>,texture2d<float,access::read>){++g_revealTaps;return g_reveal;}
bool fogCeilingCutFace(vec3,int){return g_cut;}
bool fogCeilingCutFace(vec3,int,const FogObserverData&,texture2d<uint,access::read>){return g_cut;}
"""

CASES = r"""
double blend(double a,double b,double t){return a*(1.-t)+b*t;}
double smooth(double lo,double hi,double x){
    const double t=std::clamp((x-lo)/(hi-lo),0.,1.);
    return t*t*(3.-2.*t);
}
// Independent restatement of the documented reveal model (ir_fog_apply).
vec4 fogged(double state,const FogReveal &r,int axis,vec4 lit,const FogObserverData &obs,bool body){
    const double color[]={lit.x,lit.y,lit.z};
    const double hidden[]={obs.unexploredColor.x,obs.unexploredColor.y,obs.unexploredColor.z};
    const double gray=(color[0]*.299+color[1]*.587+color[2]*.114)*.4;
    const double explored=128./255.;
    double out[3];
    for(int i=0;i<3;++i){
        out[i]=state>=explored ? blend(gray,color[i],(state-explored)/(1.-explored))
                               : blend(hidden[i],gray,state/explored);
        if(body)continue;
        if(r.gridState<explored){
            const double u=1.-smooth(0,8,r.hardDistPastRim);
            out[i]=blend(out[i],color[i],.75*u*u);
            if(obs.visionCircleCount>0&&axis!=2){
                const double cap=1.-smooth(0,2,std::max(double(r.hardDistPastRim),0.));
                out[i]=blend(out[i],color[i]*(.85+.15*state),cap);
            }
        }
        if(r.styledBand>0)
            out[i]=blend(out[i],color[i]*blend(obs.revealSurfaceTreatment.z,1.,state),r.styledBand);
    }
    return vec4(float(out[0]),float(out[1]),float(out[2]),lit.a);
}
vec4 expected(vec4 lit,bool body,float bodyState,bool cut,int face,const FogObserverData &obs){
    if(body)return bodyState<1 ? fogged(bodyState,g_reveal,face/2,lit,obs,true) : lit;
    if(cut&&g_cut){
        const double tone=obs.revealSurfaceTreatment.z;
        return vec4(float(lit.x*tone),float(lit.y*tone),float(lit.z*tone),lit.a);
    }
    return g_reveal.state>=1 ? lit : fogged(g_reveal.state,g_reveal,face/2,lit,obs,false);
}
int main(){
    // Two observers: a composite reading the wrong one fails the comparison.
    const FogObserverData observers[]={
        {vec4(0,0,0,1),3,vec4(1,.5f,.85f,0)},
        {vec4(.9f,.1f,.3f,1),0,vec4(0,0,.2f,0)},
    };
    int count=0;
    for(const FogObserverData &obs:observers)
    for(vec4 lit:{vec4(0,0,0,0),vec4(.13f,.57f,.91f,.37f),vec4(1,1,1,1)})
    for(int face=0;face<6;++face)
    for(bool body:{false,true})for(float bodyState:{0.f,.25f,128.f/255.f,254.f/255.f,1.f})
    for(bool cut:{false,true})for(bool cutCeiling:{false,true})
    for(float state:{0.f,.25f,.75f,1.f})for(float grid:{0.f,1.f})for(float band:{0.f,.5f}){
        if(!body&&bodyState!=0.f)continue;
        g_reveal=FogReveal{state,grid,1.f,band};
        g_cut=cutCeiling;
        unexploredColor=obs.unexploredColor;visionCircleCount=obs.visionCircleCount;
        revealSurfaceTreatment=obs.revealSurfaceTreatment;
        g_revealTaps=0;
        const vec4 got=compose(lit,body,bodyState,cut,face,obs);
        if(!same(got,expected(lit,body,bodyState,cut,face,obs)))return 1;
        if(got.a!=lit.a)return 2;
        if(g_revealTaps!=((body||(cut&&cutCeiling))?0:1))return 3;
        ++count;
    }
    std::printf("%d surface fog cases\n",count);
}
"""


def harness(suffix, directory, mutation):
    base = SHADERS / directory
    apply_helper = (base / f"ir_fog_apply.{suffix}").read_text()
    common = (base / f"ir_fog_common.{suffix}").read_text()
    color = (base / f"ir_fog_color.{suffix}").read_text()
    compose = extract_function(common, "fogComposeSurface")
    wrappers = (extract_function(common, "fogApplyReveal") + "\n"
                + extract_function(common, "fogApplyBody"))
    glsl = suffix == "glsl"
    prefix = "" if glsl else "fogObservers."
    if mutation == "double-fog":
        if glsl:
            compose = replace_once(compose, "fogApplyReveal(reveal, faceId >> 1, litColor)",
                                   "fogApplyReveal(reveal, faceId >> 1, "
                                   "fogApplyReveal(reveal, faceId >> 1, litColor))")
        else:
            compose = replace_once(compose,
                                   "fogApplyReveal(reveal, faceId >> 1, litColor, fogObservers)",
                                   "fogApplyReveal(reveal, faceId >> 1, fogApplyReveal(reveal, "
                                   "faceId >> 1, litColor, fogObservers), fogObservers)")
    elif mutation == "unfogged-body":
        compose = compose.replace("bodyState < 1.0f", "false").replace("bodyState < 1.0", "false")
    elif mutation == "wrong-observer-tone":
        compose = replace_once(compose, prefix + "revealSurfaceTreatment.z", "kFogCutTone")
    elif mutation == "wrong-observer-anchor":
        wrappers = replace_once(wrappers, "sourceColor, " + prefix + "unexploredColor)",
                                "sourceColor, vec4(0.0))" if glsl else "sourceColor, float4(0.0))")
    elif mutation == "lost-alpha":
        apply_helper = replace_once(apply_helper, "capTone, 0.0, 1.0), sourceColor.a)"
                                    if glsl else "capTone, 0.0f, 1.0f), sourceColor.a)",
                                    "capTone, 0.0, 1.0), 1.0)" if glsl
                                    else "capTone, 0.0f, 1.0f), 1.0f)")
    elif mutation == "lost-material":
        compose = replace_once(compose, "reveal.state >= 1.0 ? litColor" if glsl
                               else "reveal.state >= 1.0f ? litColor",
                               "reveal.state >= 1.0 ? vec4(1.0)" if glsl
                               else "reveal.state >= 1.0f ? float4(1.0f)")
    elif mutation == "reveal-for-body":
        compose = replace_once(compose, "if (body) {", "if (false) {")
    if glsl:
        call = """
vec4 compose(vec4 lit,bool body,float bodyState,bool cut,int face,const FogObserverData &obs){
    return fogComposeSurface(lit,body,bodyState,cut,vec3(0),vec3(0),face,0.f,false);
}
"""
    else:
        call = """
vec4 compose(vec4 lit,bool body,float bodyState,bool cut,int face,const FogObserverData &obs){
    return fogComposeSurface(lit,body,bodyState,cut,vec3(0),vec3(0),face,0.f,false,obs,
        texture2d<uint,access::read>{},texture2d<float,access::read>{});
}
"""
    source = (host(color) + host(apply_helper) + STUBS + host(wrappers) + host(compose)
              .replace("float4(", "vec4(").replace("vec4(1.0f)", "vec4(1,1,1,1)")
              .replace("vec4(1.0)", "vec4(1,1,1,1)").replace("vec4(0.0)", "vec4(0,0,0,0)"))
    source = source.replace("vec4(0.0))", "vec4(0,0,0,0))")
    return PREAMBLE + ADAPTER + source + call + CASES


@unittest.skipUnless(COMPILER, "surface fog composition controls require a C++ compiler")
class FogSurfaceCompositionTests(unittest.TestCase):
    def test_composition_rejects_mutations(self):
        mutations = ("production", "double-fog", "unfogged-body", "wrong-observer-tone",
                     "wrong-observer-anchor", "lost-alpha", "lost-material", "reveal-for-body")
        for suffix, directory in (("glsl", ""), ("metal", "metal/")):
            original = harness(suffix, directory, "production")
            for mutation in mutations:
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
                    self.assertEqual(build.returncode, 0, build.stderr[:4000])
                    run = subprocess.run([str(path / "fog")], capture_output=True, text=True)
                    if mutation == "production":
                        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                        self.assertIn("surface fog cases", run.stdout)
                    else:
                        self.assertIn(run.returncode, (1, 2, 3), run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
