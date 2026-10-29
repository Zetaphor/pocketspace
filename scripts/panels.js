// Draw a retained canvas with text, shapes and an image; count button clicks.
// Use SteamVR pointer input on the green rectangle to redraw the panel.
const panel = createPanel({
  widthPixels: 640, heightPixels: 360, widthMeters: .8, interactive: true
});
panelSetTransform(panel, 'viewerFacing', 0, 0, 0);
let clicks = 0;
function draw() {
  const c = panel.canvas;
  c.clear(12, 18, 29, 255);
  c.text(24, 20, 'Interactive panel', 240, 245, 255, 255, 30);
  c.line(24, 70, 616, 70, 80, 150, 220, 255);
  c.rect(24, 100, 440, 100, 30, 130, 90, 255, true);
  c.text(48, 130, `Clicks: ${clicks}`, 255, 255, 255, 255, 28);
  c.circle(530, 150, 36, 240, 190, 80, 255, false);
  c.image(24, 240, 'assets/panel-icon.png');
  // Drawing changes retained pixels; present requests their upload only after a redraw.
  c.present();
}
draw();
// Panel event coordinates are pixels, unlike model surface pointer coordinates.
onInputEvent(event => {
  if (event.type === 'panel.pointerDown' && event.objectId === panel.id &&
      event.control === 'left' && event.x >= 24 && event.x <= 464 &&
      event.y >= 100 && event.y <= 200) {
    clicks++;
    draw();
  }
});
