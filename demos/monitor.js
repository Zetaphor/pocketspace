// Performance monitor.
// Controls: grab the blue handle under the panel with the trigger or grip to
// move it. The panel shows engine FPS, CPU, RAM, driver-reported GPU memory and
// the engine's GPU allocation estimate, each with a 60-second history.
// Shows: getEngineStats, a retained canvas redrawn once per statistics sample,
// a panel following a grabbed object, lasers shown only while aimed, and saved
// data (the monitor's position).
const saved = loadScriptData('monitor') ?? {};
const handle = createObject(loadModel('assets/physics/BlueBox.glb'),
  saved.x ?? .4, saved.y ?? -.1, saved.z ?? -.3, .05);
enableObjectInteraction(handle, {grabButton: 'both'});
setObjectInteraction(handle, {depthAdjust: true});

const width = 640, height = 400, widthMeters = .3;
const panel = createPanel({widthPixels: width, heightPixels: height, widthMeters});
panelLoadFont(panel, 'assets/fonts/montserrat/Montserrat-SemiBold.ttf');
panelSetTransform(panel, 'object', 0, .035 + widthMeters * height / width / 2, 0,
  {objectId: handle, viewerFacing: true});

const HISTORY = 60, MiB = 1024 * 1024;
const bytes = value => value >= 1024 * MiB ? `${(value / 1024 / MiB).toFixed(2)} GiB`
                                          : `${Math.round(value / MiB)} MiB`;
// Rates are drawn from zero up to at least a full-scale value; memory is drawn
// between its own minimum and maximum, so small changes stay visible.
const rows = [
  {label: 'FPS', field: 'fps', format: v => v.toFixed(0), floor: 0, full: 90, color: [120, 230, 160]},
  {label: 'CPU', field: 'cpuPercent', format: v => `${v.toFixed(0)}%`, floor: 0, full: 100, color: [250, 190, 90]},
  {label: 'RAM', field: 'ramBytes', format: bytes, color: [120, 180, 250]},
  {label: 'GPU memory', field: 'vramBytes', format: bytes, color: [210, 140, 250]},
  {label: 'GPU estimate', field: 'gpuAllocatedEstimateBytes', format: bytes, color: [240, 120, 150]},
].map(row => ({...row, values: []}));

let lastSample = -1;
// Add one statistics sample to every history; missing values leave a gap.
function record(stats) {
  for (const row of rows) {
    const value = stats[row.field];
    row.values.push(Number.isFinite(value) ? value : null);
    if (row.values.length > HISTORY) row.values.shift();
  }
}

function draw() {
  const c = panel.canvas, rowHeight = 76, chartX = 250, chartW = width - chartX - 20;
  c.clear(14, 20, 32, 255);
  rows.forEach((row, i) => {
    const top = 10 + i * rowHeight, [r, g, b] = row.color;
    const current = row.values.at(-1);
    c.text(20, top + 4, row.label, 160, 180, 210, 255, 24);
    c.text(20, top + 34, current == null ? 'n/a' : row.format(current), 245, 248, 255, 255, 32);
    c.rect(chartX, top + 6, chartW, rowHeight - 18, 26, 34, 50, 255, true);
    const known = row.values.filter(v => v !== null);
    if (!known.length) return;
    let low = row.floor ?? Math.min(...known), high = Math.max(row.full ?? -Infinity, ...known);
    if (high - low < 1e-9) { low -= 1; high += 1; }
    const x = index => chartX + chartW * (index + HISTORY - row.values.length) / (HISTORY - 1);
    const y = value => top + rowHeight - 14 - (rowHeight - 22) * (value - low) / (high - low);
    row.values.forEach((value, index) => {
      const previous = row.values[index - 1];
      if (value !== null && previous != null)
        c.line(x(index - 1), y(previous), x(index), y(value), r, g, b, 255);
    });
    if (current != null) c.circle(x(row.values.length - 1), y(current), 4, r, g, b, 255, true);
  });
  c.present();
}

// Show a hand's laser only while it points at or holds the handle.
const aiming = {left: new Set(), right: new Set()};
function aim(hand, what, on) {
  if (!aiming[hand]) return;
  const before = aiming[hand].size > 0;
  if (on) aiming[hand].add(what); else aiming[hand].delete(what);
  if (before !== aiming[hand].size > 0) setLaserVisible(hand, aiming[hand].size > 0);
}
onInputEvent(event => {
  if (event.objectId !== handle || event.type.startsWith('panel.')) return;
  if (event.type === 'overlay.meshHoverStart') aim(event.hand, 'handle', true);
  else if (event.type === 'overlay.meshHoverEnd') aim(event.hand, 'handle', false);
  else if (event.type === 'overlay.grabStart') aim(event.hand, 'grab', true);
  else if (event.type === 'overlay.grabEnd') {
    aim(event.hand, 'grab', false);
    const at = getObjectTransform(handle);
    saveScriptData('monitor', {x: at.x, y: at.y, z: at.z});
  }
});

function update() {
  const stats = getEngineStats();
  if (stats.sampleId === lastSample) return;
  lastSample = stats.sampleId;
  if (stats.sampleId > 0) record(stats);
  draw();
}
update();
