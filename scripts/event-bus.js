// Messages between running scripts. The trigger publishes a shared counter; the panel
// shows the latest value from any script, and the catch-all logs other scripts and the engine.
const panel = createPanel({widthPixels: 512, heightPixels: 128, widthMeters: .4, interactive: false});
panelSetTransform(panel, 'world', 0, .2, 0);
let count = 0;
function show(text) {
  panel.canvas.clear(16, 28, 48, 240);
  panel.canvas.text(20, 44, text, 235, 245, 255, 255, 28);
  panel.canvas.present();
}
show('Pull a trigger to publish');
// An exact topic also receives this script's own publications.
subscribe('space.counter', event => {
  count = event.payload.count;
  show(`Counter ${count} from script ${event.sourceInstanceId}`);
});
// The catch-all sees every other script's topics and engine.* events, never its own.
subscribe('*', event => print(`${event.topic} #${event.sequence} from ${event.sourceInstanceId}: ${JSON.stringify(event.payload)}`));
onInputEvent(event => {
  if (event.type !== 'controller.button' || event.control !== '/input/trigger/click' || !event.down) return;
  // Payloads are copied JSON data; null means the queue is full and the message was dropped.
  if (publish('space.counter', {count: count + 1}) === null) print('Bus queue full; message dropped');
});
