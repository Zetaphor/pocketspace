// Select model animation clips with bumper; A/X pauses/resumes.
// B/Y restores the default pose; move the right stick horizontally to scrub.
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
    else if (getAnimationState(object).clip === null) play();
    else resumeAnimation(object);
  } else if (['/input/b/click', '/input/y/click'].includes(event.control)) stopAnimation(object);
});

function update(dt) {
  const stick = getControllerInput('right');
  if (!stick.valid || Math.abs(stick.analog.thumbstick.x) < .15) return;
  let state = getAnimationState(object);
  if (state.clip === null) { play(); state = getAnimationState(object); }
  // Seeking preserves playback mode; explicitly pause so scrubbing holds the chosen pose.
  pauseAnimation(object);
  seekAnimation(object, Math.max(0, Math.min(state.duration, state.time + stick.analog.thumbstick.x * dt)));
}
