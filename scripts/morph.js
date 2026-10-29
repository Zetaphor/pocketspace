// Play a grabbable morphing cube. A/X pauses/resumes; bumper restarts the clip.
const asset = loadModel('assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb');
const object = createObject(asset, 0, 0, 0, .55);
enableObjectInteraction(object, {grabButton: 'both'});
playAnimation(object, 0, {loop: true});
// Ignore duplicate button-down reports until the corresponding release.
const pressed = new Set();
onInputEvent(event => {
  if (event.type !== 'controller.button') return;
  const key = event.hand + event.control;
  if (!event.down) { pressed.delete(key); return; }
  if (pressed.has(key)) return;
  pressed.add(key);
  if (event.control === '/input/a/click' || event.control === '/input/x/click') {
    if (getAnimationState(object).playing) pauseAnimation(object);
    else resumeAnimation(object);
    print(getAnimationState(object).playing ? 'Playing' : 'Paused');
  } else if (event.control === '/input/bumper/click') {
    playAnimation(object, 0, {loop: true});
  }
});
