// Sticky notes.
// Controls: point at a colour on the dispenser and pull the trigger to take a
// note. Grab a note by its pin (trigger or grip) and leave it anywhere around
// you. Pull the trigger on a note to cycle its label: To do, Done, Idea, Call and
// its number. Drop a note's pin on the red bin to remove it. Grab the blue handle
// under the dispenser, or the bin itself, to move them.
// Shows: scene containers, spawning and removal, grabbing, panels following
// objects, scene saves with fresh IDs on restore, and saved data.
const FONT = 'assets/fonts/montserrat/Montserrat-SemiBold.ttf';
const COLORS = [[250, 225, 90], [245, 150, 175], [140, 220, 140], [130, 190, 245]];
const LABELS = ['To do', 'Done', 'Idea', 'Call'];
// A scene save holds at most 32 panels, one per note.
const MAX_NOTES = 32, NOTE_PIXELS = 256, NOTE_METERS = .12;
const pinModel = loadModel('assets/physics/YellowSphere.glb');
const saved = loadScriptData('notes') ?? {};

// Each note is a grabbable pin with its note panel hanging below it.
let notes = [], container = 0, next = saved.next ?? 1;
const labelText = note => note.label < LABELS.length ? LABELS[note.label] : `#${note.number}`;
function drawNote(note) {
  const c = note.panel.canvas, [r, g, b] = COLORS[note.color], text = labelText(note);
  c.clear(r, g, b, 255);
  c.rect(0, 0, NOTE_PIXELS, 28, r * .85, g * .85, b * .85, 255, true);
  const pixels = text.length > 4 ? 48 : 64;
  c.text((NOTE_PIXELS - (c.measureText(text, pixels) ?? 0)) / 2, 128 - pixels / 2, text, 40, 40, 48, 255, pixels);
  c.present();
}
function attach(note) {
  panelSetTransform(note.panel, 'object', 0, -.012 - NOTE_METERS / 2, 0,
    {objectId: note.pin, viewerFacing: true});
}

// Restore the saved notes; the scene save gives them fresh IDs.
if (saved.notes?.length) {
  try {
    const loaded = loadSceneContainer('notes');
    container = loaded.containerId;
    const panels = new Map(loaded.panels.map(panel => [panel.id, panel]));
    notes = saved.notes.map(note => ({...note, pin: loaded.objectMap[note.pin],
                                      panel: panels.get(loaded.panelMap[note.panel])}));
  } catch (error) {
    print(`notes: could not restore the saved notes: ${error}`);
  }
}
if (!container) container = createSceneContainer();

const handleModel = loadModel('assets/physics/BlueBox.glb');
const dispenserAt = saved.dispenser ?? {x: 0, y: -.25, z: -.35};
const handle = createObject(handleModel, dispenserAt.x, dispenserAt.y, dispenserAt.z, .05);
enableObjectInteraction(handle, {grabButton: 'both'});
setObjectInteraction(handle, {depthAdjust: true});
const dispenser = createPanel({widthPixels: 512, heightPixels: 160, widthMeters: .24, interactive: true});
panelLoadFont(dispenser, FONT);
panelSetTransform(dispenser, 'object', 0, .035 + .24 * 160 / 512 / 2, 0, {objectId: handle, viewerFacing: true});

const binAt = saved.bin ?? {x: .25, y: -.3, z: -.35};
const bin = createObject(loadModel('assets/samples/Box/Box.glb'), binAt.x, binAt.y, binAt.z, .12);
enableObjectInteraction(bin, {grabButton: 'both'});
setObjectInteraction(bin, {depthAdjust: true});
const binLabel = createPanel({widthPixels: 256, heightPixels: 64, widthMeters: .12});
panelLoadFont(binLabel, FONT);
panelSetTransform(binLabel, 'object', 0, .1, 0, {objectId: bin, viewerFacing: true});
binLabel.canvas.clear(30, 30, 36, 255);
binLabel.canvas.text((256 - (binLabel.canvas.measureText('Bin', 36) ?? 0)) / 2, 12, 'Bin', 255, 255, 255, 255, 36);
binLabel.present();

function drawDispenser() {
  const c = dispenser.canvas, full = notes.length >= MAX_NOTES;
  c.clear(34, 40, 52, 255);
  COLORS.forEach(([r, g, b], i) => c.rect(16 + i * 124, 16, 108, 96, r, g, b, full ? 90 : 255, true));
  const text = full ? 'Full: drop notes in the bin' : 'Pick a colour for a new note';
  c.text((512 - (c.measureText(text, 24) ?? 0)) / 2, 122, text, 200, 210, 225, 255, 24);
  c.present();
}

function save() {
  const at = id => { const {x, y, z} = getObjectTransform(id); return {x, y, z}; };
  saveSceneContainer(container, 'notes');
  saveScriptData('notes', {
    dispenser: at(handle), bin: at(bin), next,
    notes: notes.map(({pin, panel, color, label, number}) => ({pin, panel: panel.id, color, label, number})),
  });
}
function spawn(color) {
  if (notes.length >= MAX_NOTES) return;
  const at = getObjectTransform(handle), offset = (notes.length % 5) * .03;
  const pin = createObject(pinModel, at.x - .06 + offset, at.y + .26, at.z, .03, {container, renderGroup: 1});
  enableObjectInteraction(pin, {grabButton: 'both'});
  setObjectInteraction(pin, {depthAdjust: true});
  const panel = createPanel({widthPixels: NOTE_PIXELS, heightPixels: NOTE_PIXELS,
                             widthMeters: NOTE_METERS, interactive: true, container});
  panelLoadFont(panel, FONT);
  const note = {pin, panel, color, label: 0, number: next++};
  attach(note);
  drawNote(note);
  notes.push(note);
  drawDispenser();
  save();
}
function remove(note) {
  for (const hand of ['left', 'right']) {
    aim(hand, `panel${note.panel.id}`, false);
    aim(hand, `object${note.pin}`, false);
  }
  removePanel(note.panel);
  removeObject(note.pin);
  notes = notes.filter(other => other !== note);
  drawDispenser();
  save();
}
// A note counts as dropped in the bin when its pin lands within the bin's box.
function inBin(pin) {
  const a = getObjectTransform(pin), b = getObjectTransform(bin);
  return Math.abs(a.x - b.x) < .08 && Math.abs(a.y - b.y) < .1 && Math.abs(a.z - b.z) < .08;
}

// Show a hand's laser only while it points at something here or holds it.
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}
onInputEvent(event => {
  const {type, hand} = event;
  if (type.startsWith('panel.')) {
    const note = notes.find(note => note.panel.id === event.objectId);
    if (!note && event.objectId !== dispenser.id) return;
    if (type === 'panel.hoverStart') aim(hand, `panel${event.objectId}`, true);
    else if (type === 'panel.hoverEnd') aim(hand, `panel${event.objectId}`, false);
    else if (type === 'panel.pointerDown' && event.control === 'left') {
      if (note) {
        note.label = (note.label + 1) % (LABELS.length + 1);
        drawNote(note);
        save();
      } else if (event.y < 112) {
        const color = Math.floor((event.x - 16) / 124);
        if (color >= 0 && color < COLORS.length && event.x - 16 - color * 124 < 108) spawn(color);
      }
    }
    return;
  }
  const note = notes.find(note => note.pin === event.objectId);
  if (!note && event.objectId !== handle && event.objectId !== bin) return;
  if (type === 'overlay.meshHoverStart') aim(hand, `object${event.objectId}`, true);
  else if (type === 'overlay.meshHoverEnd') aim(hand, `object${event.objectId}`, false);
  else if (type === 'overlay.grabStart') aim(hand, 'grab', true);
  else if (type === 'overlay.grabEnd') {
    aim(hand, 'grab', false);
    if (note && inBin(note.pin)) remove(note);
    else save();
  }
});
drawDispenser();
