"""Replay the isolated native GPU evidence; no index construction or frame timing."""
import importlib
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "scripts/perf"))
sys.path.insert(0, str(ROOT / "scripts/tests"))
helpers = importlib.import_module("shader_contract_helpers")
probe = importlib.import_module("sun_query_probe")
extract_function = helpers.extract_function
replace_once = helpers.replace_once
query = helpers.extract_block(probe.KERNEL, "kernel void probe(")
query = replace_once(query,
    "0.25+float2(id%256u,(id/256u)%256u)/512.0", "float2(2.5,3.5)")
query = replace_once(query, "point,2.0", "point,10.0")
query = replace_once(query, "buffer,128.0,true", "buffer,100.0,false")
probe.KERNEL = r"""
kernel void initialize(device uint* buffer [[buffer(0)]],
                       constant uint2& config [[buffer(1)]],
                       uint id [[thread_position_in_grid]]) {
    if(id!=0u)return;
    buffer[kSourceFaceHeaderOffset]=1u;
    buffer[sourceFaceTileBase(0)]=0u;
    buffer[kSourceFaceSpillCounterOffset]=0u;
    const uint kind=config.y;
    const uint low=(uint((2.0+512.0)*1024.0)<<8u);
    const uint high=(uint((-120.0+512.0)*1024.0)<<8u);
    buffer[3u*1024u+2u]=kind==2u?high:(kind==4u?0xffffffffu:low);
    buffer[kSourceFaceFallbackOffset+3u*1024u+2u]=kind==0u?0xffffffffu:
        ((kind==1u?high:low)|0x89u);
}
""" + query
probe.LAUNCHER = replace_once(probe.LAUNCHER, "kind>=3?0x3f800000u:0u", "0x3f800000u")
probe.LAUNCHER = replace_once(probe.LAUNCHER,
    "const uint32_t expected[]={1,1,visits,misses,0,kind>=3?1u:0u,\n"
    "                    kind>=3?visits:0u,kind<3?1u:0u,0,0};",
    "const uint32_t expected[]={1,0,0,0,0,0,0,0,0,0};")
if "--parent" in sys.argv:
    sys.argv.remove("--parent")
    path="engine/render/src/shaders/metal/ir_sun_shadow_sample.metal"
    parent=subprocess.check_output(["git","show","ec026219cf65b9f0560bf4241523fa7e1aa4e4b7:"+path],cwd=probe.ROOT,text=True)
    current=(probe.ROOT/path).read_text()
    old=extract_function(parent,"sampleCascadeShadow")
    new=extract_function(current,"sampleCascadeShadow")
    source=probe.shader_source
    probe.shader_source=lambda diagnostic:replace_once(source(diagnostic),new,old)
probe.main()
