// Desk clock and countdown timer.
// Controls: point at the clock and pull the trigger to press its buttons. 1, 5,
// 15 and 25 set the timer; Start/Pause and Reset run it; 12h/24h switches the
// clock. At zero the panel flashes and both controllers buzz for ten seconds or
// until the next press. Grab the blue handle under the clock to move it.
// Shows: retained-canvas drawing, panel buttons and hover, haptics, a panel
// following a grabbed object, lasers shown only while aimed, and saved data
// (position, last duration and clock format).
const saved = loadScriptData('clock') ?? {};
const handle = createObject(loadModel('assets/physics/BlueBox.glb'),
  saved.x ?? -.4, saved.y ?? -.1, saved.z ?? -.3, .05);
enableObjectInteraction(handle, {grabButton: 'both'});
setObjectInteraction(handle, {depthAdjust: true});

const width = 640, height = 460, widthMeters = .32;
const panel = createPanel({widthPixels: width, heightPixels: height, widthMeters, interactive: true});
panelLoadFont(panel, 'assets/fonts/montserrat/Montserrat-SemiBold.ttf');
// The panel stands on the handle and turns to face you wherever it is moved.
panelSetTransform(panel, 'object', 0, .035 + widthMeters * height / width / 2, 0,
  {objectId: handle, viewerFacing: true});

let duration = saved.duration ?? 300, hour12 = saved.hour12 ?? false;
let remaining = duration, endsAt = 0, running = false;
let alarmUntil = 0, hovered = -1, drawnKey = '';
const ALARM_MS = 10000;

const buttons = [
  ...[1, 5, 15, 25].map((minutes, i) => ({
    label: `${minutes}`, x: 24 + i * 152, y: 296, w: 136, h: 64,
    press: () => { duration = minutes * 60; reset(); save(); }})),
  {label: () => running ? 'Pause' : 'Start', x: 24, y: 376, w: 200, h: 64, press: startPause},
  {label: 'Reset', x: 240, y: 376, w: 176, h: 64, press: () => reset()},
  {label: () => hour12 ? '12h' : '24h', x: 432, y: 376, w: 184, h: 64,
   press: () => { hour12 = !hour12; save(); }},
];

function save() {
  const at = getObjectTransform(handle);
  saveScriptData('clock', {x: at.x, y: at.y, z: at.z, duration, hour12});
}
function left(now) { return running ? Math.max(0, (endsAt - now) / 1000) : remaining; }
function startPause() {
  const now = Date.now();
  if (running) { remaining = left(now); running = false; return; }
  if (remaining <= 0) remaining = duration;
  endsAt = now + remaining * 1000;
  running = true;
}
function reset() { running = false; remaining = duration; alarmUntil = 0; }

const pad = value => String(value).padStart(2, '0');
const DAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
function clockText(date) {
  const hours = date.getHours(), minutes = pad(date.getMinutes());
  if (!hour12) return `${pad(hours)}:${minutes}`;
  return `${hours % 12 || 12}:${minutes} ${hours < 12 ? 'AM' : 'PM'}`;
}
function timerText(seconds) {
  const whole = Math.ceil(seconds);
  return `${pad(Math.floor(whole / 60))}:${pad(whole % 60)}`;
}

function centered(c, text, y, pixels, r, g, b, x = 0, w = width) {
  c.text(x + (w - (c.measureText(text, pixels) ?? 0)) / 2, y, text, r, g, b, 255, pixels);
}
// Redraw only when what the panel shows changes: the minute, the timer's
// second, the hovered button, the run state or the alarm flash.
function draw(now) {
  const date = new Date(now), alarm = now < alarmUntil;
  const flash = alarm && Math.floor((alarmUntil - now) / 500) % 2 === 0;
  const key = [clockText(date), date.toDateString(), timerText(left(now)), running,
               hour12, hovered, alarm, flash].join('|');
  if (key === drawnKey) return;
  drawnKey = key;
  const c = panel.canvas;
  if (flash) c.clear(150, 40, 30, 255); else c.clear(14, 20, 32, 255);
  centered(c, clockText(date), 18, 96, 245, 248, 255);
  centered(c, `${DAYS[date.getDay()]} ${date.getDate()} ${MONTHS[date.getMonth()]}`, 132, 32, 160, 180, 210);
  c.line(24, 184, width - 24, 184, 60, 80, 110, 255);
  const timerColor = alarm ? [255, 220, 120] : running ? [120, 230, 160] : [220, 225, 235];
  centered(c, timerText(left(now)), 200, 64, ...timerColor);
  for (const button of buttons) {
    const active = button.label === `${duration / 60}`;
    const shade = buttons.indexOf(button) === hovered ? 70 : 0;
    if (active) c.rect(button.x, button.y, button.w, button.h, 40 + shade, 110 + shade, 200, 255, true);
    else c.rect(button.x, button.y, button.w, button.h, 32 + shade, 42 + shade, 60 + shade, 255, true);
    const label = typeof button.label === 'function' ? button.label() : button.label;
    centered(c, label, button.y + 14, 32, 255, 255, 255, button.x, button.w);
  }
  c.present();
}

// The index of the button under (x, y), or -1.
const hit = (x, y) => buttons.findIndex(b => x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h);
// Show a hand's laser only while it points at the clock or holds the handle.
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}

onInputEvent(event => {
  const onPanel = event.objectId === panel.id && event.type.startsWith('panel.');
  const onHandle = event.objectId === handle && !event.type.startsWith('panel.');
  if (onPanel) {
    if (event.type === 'panel.hoverStart' || event.type === 'panel.hover') {
      aim(event.hand, 'panel', true);
      hovered = hit(event.x, event.y);
    } else if (event.type === 'panel.hoverEnd') {
      aim(event.hand, 'panel', false);
      hovered = -1;
    } else if (event.type === 'panel.pointerDown' && event.control === 'left') {
      // A press during the alarm only silences it.
      if (Date.now() < alarmUntil) alarmUntil = 0;
      else buttons[hit(event.x, event.y)]?.press();
    }
  } else if (onHandle) {
    if (event.type === 'overlay.meshHoverStart') aim(event.hand, 'handle', true);
    else if (event.type === 'overlay.meshHoverEnd') aim(event.hand, 'handle', false);
    else if (event.type === 'overlay.grabStart') aim(event.hand, 'grab', true);
    else if (event.type === 'overlay.grabEnd') { aim(event.hand, 'grab', false); save(); }
  } else return;
  draw(Date.now());
});

let nextPulse = 0;
function update() {
  const now = Date.now();
  if (running && now >= endsAt) {
    running = false;
    remaining = 0;
    alarmUntil = now + ALARM_MS;
    nextPulse = now;
  }
  if (now < alarmUntil && now >= nextPulse) {
    pulse('left', .2, .8);
    pulse('right', .2, .8);
    nextPulse = now + 1000;
  }
  draw(now);
}
draw(Date.now());
