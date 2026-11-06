// Sketch board.
// Controls: point at the board and hold the trigger to draw; either hand can
// draw at once. The toolbar picks a colour and brush size; Undo removes the last
// stroke or clear, and Clear empties the board. The drawing is saved after every
// stroke and restored on the next start.
// Shows: panel hover and pointer events with pixel coordinates, canvas lines and
// circles on a retained canvas, partial toolbar redraws, lasers shown only while aimed,
// and saved data (the strokes).
const width = 1024, height = 704, toolbar = 96;
const panel = createPanel({widthPixels: width, heightPixels: height, widthMeters: .8, interactive: true});
panelLoadFont(panel, 'assets/fonts/montserrat/Montserrat-SemiBold.ttf');
panelSetTransform(panel, 'world', 0, .05, 0);
const c = panel.canvas;

const COLORS = [[20, 22, 28], [215, 50, 45], [235, 140, 30], [40, 150, 70], [40, 100, 215], [130, 60, 180]];
const SIZES = [3, 7, 14];
// Saved strokes must fit the 64 KiB saved-data limit, with room to spare.
const LIMIT = 63 * 1024, POINT_CHARS = 10, STROKE_CHARS = 24;
const pointChars = (x, y) => String(x).length + String(y).length + 2;

// Strokes are {c: colour index, s: size index, p: [x0, y0, x1, y1, ...]}.
// History holds strokes and clears so both can be undone.
const history = (loadScriptData('sketch')?.strokes ?? []).map(stroke => ({stroke}));
let color = 0, size = 1, full = false, hovered = -1;
const drawing = {};
const visible = () => {
  const start = history.findLastIndex(entry => entry.clear) + 1;
  return history.slice(start).map(entry => entry.stroke);
};
let used = JSON.stringify({strokes: visible()}).length;

const tools = [
  ...COLORS.map((rgb, i) => ({x: 16 + i * 80, kind: 'color', index: i})),
  ...SIZES.map((radius, i) => ({x: 516 + i * 80, kind: 'size', index: i})),
  {x: 772, w: 116, kind: 'undo', label: 'Undo'},
  {x: 896, w: 112, kind: 'clear', label: 'Clear'},
].map(tool => ({y: 12, w: 72, h: 72, ...tool}));

function dot(stroke, x, y) {
  const [r, g, b] = COLORS[stroke.c];
  c.circle(x, y, SIZES[stroke.s], r, g, b, 255, true);
}
// Join two points with overlapping filled circles for an even, round line.
function segment(stroke, x0, y0, x1, y1) {
  const steps = Math.max(1, Math.ceil(Math.hypot(x1 - x0, y1 - y0) / Math.max(1, SIZES[stroke.s] / 2)));
  for (let i = 1; i <= steps; i++) dot(stroke, x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps);
}
function paint(stroke) {
  const p = stroke.p;
  dot(stroke, p[0], p[1]);
  for (let i = 2; i < p.length; i += 2) segment(stroke, p[i - 2], p[i - 1], p[i], p[i + 1]);
}

function drawToolbar() {
  c.rect(0, 0, width, toolbar, 34, 40, 52, 255, true);
  tools.forEach((tool, i) => {
    const selected = tool.kind === 'color' ? tool.index === color : tool.kind === 'size' && tool.index === size;
    const shade = i === hovered ? 40 : 0;
    // A full board marks Undo and Clear, the ways to make room.
    if (full && tool.label) c.rect(tool.x, tool.y, tool.w, tool.h, 150 + shade, 50 + shade, 45 + shade, 255, true);
    else c.rect(tool.x, tool.y, tool.w, tool.h, 60 + shade, 68 + shade, 84 + shade, 255, true);
    if (selected) c.rect(tool.x, tool.y, tool.w, tool.h, 255, 255, 255, 255);
    const cx = tool.x + tool.w / 2, cy = tool.y + tool.h / 2;
    if (tool.kind === 'color') c.circle(cx, cy, 24, ...COLORS[tool.index], 255, true);
    else if (tool.kind === 'size') c.circle(cx, cy, SIZES[tool.index], ...COLORS[color], 255, true);
    else c.text(cx - (c.measureText(tool.label, 28) ?? 0) / 2, tool.y + 20, tool.label, 255, 255, 255, 255, 28);
  });
  c.present();
}
function drawBoard() {
  c.rect(0, toolbar, width, height - toolbar, 250, 250, 246, 255, true);
  for (const stroke of visible()) paint(stroke);
  for (const stroke of Object.values(drawing)) paint(stroke);
  c.present();
}

function save() {
  const strokes = visible();
  saveScriptData('sketch', {strokes});
  used = JSON.stringify({strokes}).length;
  full = used + STROKE_CHARS + 2 * POINT_CHARS > LIMIT;
}
// Keep points on the board, below the toolbar, so strokes never paint over it.
function onBoard(stroke, x, y) {
  const radius = SIZES[stroke.s];
  return [Math.round(Math.min(width - radius, Math.max(radius, x))),
          Math.round(Math.min(height - radius, Math.max(toolbar + radius, y)))];
}
function begin(hand, x, y) {
  if (full) return;
  const stroke = {c: color, s: size, p: []};
  stroke.p.push(...onBoard(stroke, x, y));
  used += STROKE_CHARS + pointChars(stroke.p[0], stroke.p[1]);
  drawing[hand] = stroke;
  dot(stroke, stroke.p[0], stroke.p[1]);
  c.present();
}
function extend(hand, x, y) {
  const stroke = drawing[hand];
  if (!stroke) return;
  const [px, py] = onBoard(stroke, x, y), p = stroke.p;
  const lx = p[p.length - 2], ly = p[p.length - 1];
  if (Math.hypot(px - lx, py - ly) < 3) return;
  // A stroke that would overflow the saved data ends here.
  if (used + pointChars(px, py) > LIMIT) { finish(hand); return; }
  used += pointChars(px, py);
  p.push(px, py);
  segment(stroke, lx, ly, px, py);
  c.present();
}
function finish(hand) {
  const stroke = drawing[hand];
  if (!stroke) return;
  delete drawing[hand];
  history.push({stroke});
  save();
  drawToolbar();
}
function press(tool) {
  if (tool.kind === 'color') color = tool.index;
  else if (tool.kind === 'size') size = tool.index;
  else if (tool.kind === 'undo') {
    if (!history.length) return;
    history.pop();
    save();
    drawBoard();
  } else if (tool.kind === 'clear') {
    if (!visible().length) return;
    history.push({clear: true});
    save();
    drawBoard();
  }
  drawToolbar();
}

const toolAt = (x, y) => tools.findIndex(t => x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h);
const aimed = {left: false, right: false};
onInputEvent(event => {
  if (event.objectId !== panel.id || !event.type.startsWith('panel.')) return;
  const hand = event.hand;
  if (event.type === 'panel.hoverStart' || event.type === 'panel.hoverEnd') {
    const on = event.type === 'panel.hoverStart';
    if (hand in aimed && aimed[hand] !== on) { aimed[hand] = on; setLaserVisible(hand, on); }
  }
  if (event.type === 'panel.hover') {
    extend(hand, event.x, event.y);
    const tool = event.y < toolbar ? toolAt(event.x, event.y) : -1;
    if (tool !== hovered) { hovered = tool; drawToolbar(); }
  } else if (event.type === 'panel.hoverEnd' && hovered !== -1) {
    hovered = -1;
    drawToolbar();
  } else if (event.type === 'panel.pointerDown' && event.control === 'left') {
    if (event.y >= toolbar) begin(hand, event.x, event.y);
    else if (toolAt(event.x, event.y) >= 0) press(tools[toolAt(event.x, event.y)]);
  } else if (event.type === 'panel.pointerUp' && event.control === 'left') {
    finish(hand);
  }
});

full = used + STROKE_CHARS + 2 * POINT_CHARS > LIMIT;
drawToolbar();
drawBoard();
