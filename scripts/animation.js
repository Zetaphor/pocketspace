const asset = loadModel('assets/samples/InterpolationTest/InterpolationTest.glb');
const object = createObject(asset, 0, 0, 0, .65);
const clips = getAnimations(asset);
let selected = clips.findIndex(clip => clip.name === 'Linear Rotation');
function play() {
  playAnimation(object, selected, {loop: true});
  print(clips[selected].name);
}
play();
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/bumper/click') {
    selected = (selected + 1) % clips.length;
    play();
  } else if (event.control === '/input/a/click' || event.control === '/input/x/click') {
    if (getAnimationState(object).playing) pauseAnimation(object);
    else resumeAnimation(object);
  }
});
