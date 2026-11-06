// canvas.measureText against the canvas's own text drawing.
function check(condition) { if (!condition) throw Error('canvas text assertion'); }
function fails(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
const panel = createPanel({widthPixels: 512, heightPixels: 128, widthMeters: .4});
panelLoadFont(panel, 'assets/fonts/montserrat/Montserrat-Medium.ttf');
const c = panel.canvas;
check(c.measureText('') === 0);
const hello = c.measureText('Hello', 32), world = c.measureText('Hello world', 32);
check(Number.isInteger(hello) && hello > 0 && world > hello);
check(c.measureText('Hello') === hello);
check(Math.abs(c.measureText('Hello', 64) - 2 * hello) <= 2);
// Several lines measure as the widest line.
check(c.measureText('Hello\nHello world', 32) === world);
fails(() => c.measureText());
c.clear(0, 0, 0, 255);
c.text(10, 10, 'Hello world', 255, 255, 255, 255, 32);
globalThis.measured = {hello, world};
// Without a usable font there is no width, as text() draws nothing.
const missing = createPanel({widthPixels: 8, heightPixels: 8, widthMeters: .1});
panelLoadFont(missing, '/missing-font.ttf');
check(missing.canvas.measureText('Hi') === null && missing.canvas.text(0, 0, 'Hi') === false);
removePanel(missing);
fails(() => missing.canvas.measureText('Hi'));
