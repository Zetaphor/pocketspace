// This gallery does not start animation clips; it shows default skinned poses
// and unmorphed geometry. Use skinning-test.js for skeletal playback.
const alpha = loadModel("assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb");
const man = loadModel("assets/samples/CesiumMan/CesiumMan.glb");
const morph = loadModel("assets/samples/MorphPrimitivesTest/MorphPrimitivesTest.glb");
createObject(alpha, -0.17, 0, 0, 0.18);
createObject(man, 0, 0, 0, 0.3);
createObject(morph, 0.17, 0, 0, 0.18);
function update(dt) {}
