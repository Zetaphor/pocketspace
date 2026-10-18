// Static binding/morph defaults are shown; animation playback is not implemented.
const alpha = loadModel("assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb");
const man = loadModel("assets/samples/CesiumMan/CesiumMan.glb");
const morph = loadModel("assets/samples/MorphPrimitivesTest/MorphPrimitivesTest.glb");
createObject(alpha, -0.17, 0, 0, 0.18);
createObject(man, 0, 0, 0, 0.3);
createObject(morph, 0.17, 0, 0, 0.18);
function update(dt) {}
