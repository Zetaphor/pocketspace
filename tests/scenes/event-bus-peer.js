// Headset peer for scripts/event-bus.js: run both together. Display-only panel.
// Trigger (in event-bus.js) publishes the counter; this script replies with
// space.counterSeen on the next frame. A or X publishes space.peerPing.
const panel = createPanel({widthPixels: 640, heightPixels: 220, widthMeters: .5, interactive: false});
panelSetTransform(panel, 'world', 0, -.08, 0);
const recent = [];
let counter = 'none';
function show() {
  const c = panel.canvas;
  c.clear(30, 20, 44, 240);
  c.text(16, 10, `Peer script: counter ${counter}`, 245, 225, 255, 255, 26);
  recent.forEach((line, i) => c.text(16, 52 + i * 32, line, 200, 220, 255, 255, 18));
  c.present();
}
function remember(line) {
  recent.unshift(line);
  recent.length = Math.min(recent.length, 5);
  print(`[peer] ${line}`);
  show();
}
show();
subscribe('space.counter', event => {
  counter = event.payload.count;
  publish('space.counterSeen', {count: counter});
  remember(`counter ${counter} from ${event.sourceInstanceId} #${event.sequence}`);
});
subscribe('*', event => {
  if (event.topic === 'space.counter') return;
  remember(`${event.topic} from ${event.sourceInstanceId}: ${JSON.stringify(event.payload)}`);
});
onInputEvent(event => {
  if (event.type !== 'controller.button' || !event.down) return;
  if (event.control === '/input/a/click' || event.control === '/input/x/click') publish('space.peerPing', {hand: event.hand});
});
