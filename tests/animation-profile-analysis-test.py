import importlib.util
import json
from pathlib import Path
spec=importlib.util.spec_from_file_location('analyzer',Path(__file__).with_name('analyze-animation-profile.py'))
analyzer=importlib.util.module_from_spec(spec);spec.loader.exec_module(analyzer)
lines=['ANIMATION_STAGE '+json.dumps(dict(stage=0,count=8,playing=False,model='test',eyePixels=768,renderGroup=1))]
row=dict(objects=8,playing=0,frames=120,seconds=2,workP95Ms=3,**{key:0 for key in ['poseMs','deformBoundsMs','bvhMs','uploadSubmitMs','renderSubmitMs','gpuWaitMs','workMs','evaluations','vertices','uploads','uploadBytes']})
for _ in range(5):lines.append('ANIMATION_PROFILE '+json.dumps(row))
lines.append('ANIMATION_PROFILE_DONE')
result=analyzer.summarize(lines);assert result[0]['windows']==2 and result[0]['frames']==240 and result[0]['fps']==60
for invalid in [lines[:-1],lines+['ANIMATION_PROFILE_ABORT']]:
    try:analyzer.summarize(invalid)
    except ValueError:pass
    else:raise AssertionError('incomplete run accepted')
bad=lines.copy();bad[1]='ANIMATION_PROFILE '+json.dumps(dict(row,objects=16))
try:analyzer.summarize(bad)
except ValueError:pass
else:raise AssertionError('mixed workload accepted')
print('PASS animation profile analysis: warmup exclusion, weighted FPS, completion and boundary rejection')

assert analyzer.summarize(['[js] '+line if line.startswith(('ANIMATION_STAGE ','ANIMATION_PROFILE_DONE')) else line for line in lines])[0]['frames']==240
