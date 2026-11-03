// Headset check for error notifications and automatic restart. Display-only panel.
// Press B or Y to throw from the input callback. With autoRestart enabled the engine
// reloads this script after 1, 2, 4, 8 and 16 seconds, then leaves it stopped.
const panel = createPanel({widthPixels: 512, heightPixels: 128, widthMeters: .4, interactive: false});
panelSetTransform(panel, 'world', 0, .1, 0);
let seconds = 0, shown = -1;
function update(dt) {
  seconds += dt;
  if (Math.floor(seconds) === shown) return;
  shown = Math.floor(seconds);
  panel.canvas.clear(48, 20, 20, 240);
  panel.canvas.text(20, 20, `Running for ${shown} s`, 255, 230, 220, 255, 28);
  panel.canvas.text(20, 70, 'Press B or Y to crash this script', 255, 200, 190, 255, 20);
  panel.canvas.present();
}
onInputEvent(event => {
  if (event.type === 'controller.button' && event.down && (event.control === '/input/b/click' || event.control === '/input/y/click'))
    throw Error(`Requested crash from the ${event.hand} controller`);
});
