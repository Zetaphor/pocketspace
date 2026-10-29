const asset = loadModel('assets/samples/AnimatedMorphCube/AnimatedMorphCube.glb');
const object = createObject(asset, 0, 0, 0, .55);
enableObjectInteraction(object, {grabButton: 'both'});
playAnimation(object, 0, {loop: true});
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/a/click' || event.control === '/input/x/click') {
    if (getAnimationState(object).playing) pauseAnimation(object);
    else resumeAnimation(object);
  } else if (event.control === '/input/bumper/click') {
    playAnimation(object, 0, {loop: true});
  }
});
