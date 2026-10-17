import importlib
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(root / 'scripts/tests'))
boundary = importlib.import_module("test_render_projected_face_boundary")
extract_function = importlib.import_module("shader_contract_helpers").extract_function

parent = "f54b28254"

for suffix, folder in [('glsl', ''), ('metal', 'metal/')]:
    path = f'engine/render/src/shaders/{folder}ir_sun_face_query_layout.{suffix}'
    old = subprocess.run(
        ['git', 'show', f'{parent}:{path}'],
        cwd=root, check=True, capture_output=True, text=True).stdout
    old = extract_function(old, 'sourceFaceRaySeparation').replace(
        'sourceFaceRaySeparation', 'parentSourceFaceRaySeparation')
    old = old.replace('float2', 'vec2').replace('float3', 'vec3')
    check = r'''
#include <cstring>
float checkedSeparation(vec2 p,float z,vec3 c,vec3 u,vec3 v){
    float current=sourceFaceRaySeparation(p,z,c,u,v);
    float parent=parentSourceFaceRaySeparation(p,z,c,u,v);
    if(std::memcmp(&current,&parent,sizeof(float))!=0){
        std::cerr<<"returned separation bits differ\n";std::exit(20);
    }
    return current;
}
'''
    source = boundary.query_source(suffix, None).replace(
        boundary.CASES,
        old + check + boundary.CASES.replace('sourceFaceRaySeparation(', 'checkedSeparation('))
    with tempfile.TemporaryDirectory() as folder:
        cpp = Path(folder) / 'parity.cpp'
        exe = Path(folder) / 'parity'
        cpp.write_text(source)
        subprocess.run([boundary.COMPILER, '-std=c++17', '-O2', '-ffp-contract=off',
                        str(cpp), '-o', str(exe)], check=True)
        run = subprocess.run([str(exe)], check=True, capture_output=True, text=True)
        print(suffix, 'bit-exact parent separation parity:', run.stdout.strip())
