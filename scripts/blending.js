const models = [
  ['InterpolationTest', -.6], ['CesiumMan', 0], ['AnimatedMorphCube', .6]
].map(([name, x]) => {
  const asset = loadModel(`assets/samples/${name}/${name}.glb`);
  const object = createObject(asset, x, 0, 0, .4);
  enableObjectInteraction(object, {grabButton: 'both'});
  playAnimation(object, 0);
  return {object, clips: getAnimations(asset), selected: 0, phase: false};
});
const pressed = new Set();
onInputEvent(event => {
  if (event.type !== 'controller.button') return;
  const key = event.hand + event.control;
  if (!event.down) { pressed.delete(key); return; }
  if (pressed.has(key)) return;
  pressed.add(key);
  for (const model of models) {
    if (event.control === '/input/bumper/click') {
      model.selected = (model.selected + 1) % model.clips.length;
      model.phase = !model.phase;
      const time = model.clips.length === 1 && model.phase ? model.clips[0].duration / 2 : 0;
      playAnimation(model.object, model.selected, {time, blendDuration: 1});
    } else if (event.control === '/input/a/click' || event.control === '/input/x/click') {
      if (getAnimationState(model.object).playing) pauseAnimation(model.object);
      else resumeAnimation(model.object);
    }
  }
});
