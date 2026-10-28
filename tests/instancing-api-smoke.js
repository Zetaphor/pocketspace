function check(value) { if (!value) throw Error('instancing API assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
const path = 'assets/samples/Box/Box.glb';
const asset = loadModel(path, 768, {gpuInstancing: true});
check(loadModel(path) === asset);
check(loadModel(path, 768, {}) === asset);
check(loadModel(path, 768, {gpuInstancing: true}) === asset);
rejects(() => loadModel(path, 768, {gpuInstancing: false}));
for (const value of [null, 0, 1, 'true', [], {}]) rejects(() => loadModel(path, 768, {gpuInstancing: value}));
rejects(() => loadModel(path, 768, {get gpuInstancing() { throw Error('getter failure'); }}));
const original = loadModel('assets/Avocado.glb');
check(loadModel('assets/Avocado.glb', 768, {gpuInstancing: false}) === original);
rejects(() => loadModel('assets/Avocado.glb', 768, {gpuInstancing: true}));
function update() {}
