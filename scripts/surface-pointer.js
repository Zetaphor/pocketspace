// Read SteamVR's native model-surface pointer and optional action-based trigger input.
// Aim with the SteamVR pointer; click to print surface coordinates and request haptics.
const box = createObject(loadModel('assets/samples/Box/Box.glb'), 0, 0, 0, .3);
// The one-argument form enables custom interaction without automatic grabbing.
enableObjectInteraction(box);
setObjectInteraction(box, {laserPassThrough: false});
let down = false;
function update() {
  // Surface-local coordinates are not mesh UVs or panel pixels; native pointer availability needs SteamVR focus.
  const pointer = getPointer(box);
  if (pointer.valid && pointer.down && !down) {
    print(`Surface pointer: ${pointer.x}, ${pointer.y}`);
    if (pointer.hand) {
      const action = getInput(pointer.hand);
      print(`Action input available: ${action.valid}; haptic accepted: ${pulse(pointer.hand, .04, .35)}`);
    }
  }
  down = pointer.valid && pointer.down;
}
