function check(value) { if (!value) throw Error('LOD API assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
const path = 'assets/samples/Box/Box.glb';
const asset = loadModel(path);
check(loadModel(path,768,{lod:true}) === asset);
check(loadModel(path,768,{}) === asset);
rejects(()=>loadModel(path,768,{lod:false}));
for(const lod of [null,0,1,'true',[],{}]) rejects(()=>loadModel(path,768,{lod}));
rejects(()=>loadModel(path,768,{get lod(){throw Error('getter failure');}}));
const full = loadModel('assets/Avocado.glb',768,{lod:false});
check(loadModel('assets/Avocado.glb')===full);
check(loadModel('assets/Avocado.glb',768,{lod:false})===full);
rejects(()=>loadModel('assets/Avocado.glb',768,{lod:true}));
function update() {}
