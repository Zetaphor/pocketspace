// Static binding/morph defaults are shown; animation playback is not implemented.
const alpha = loadModel("assets/samples/AlphaBlendModeTest/AlphaBlendModeTest.glb");
const man = loadModel("assets/samples/CesiumMan/CesiumMan.glb");
const morph = loadModel("assets/samples/MorphPrimitivesTest/MorphPrimitivesTest.glb");
createObject(alpha, -0.23, 0, 0, 0.22);
createObject(man, 0, 0, 0, 0.3);
createObject(morph, 0.23, 0, 0, 0.22);
function update(dt) {}
