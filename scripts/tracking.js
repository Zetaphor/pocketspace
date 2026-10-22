const panel = createPanel({widthPixels: 1024, heightPixels: 640, widthMeters: 1.1});
let lastDraw = 0;
function value(field) { return field.value === null ? '--' : field.value.toFixed(2); }
function update() {
  const now = Date.now();
  if (now - lastDraw < 200) return;
  lastDraw = now;
  const gaze = getCalibratedEyeTracking();
  const c = panel.canvas;
  c.clear(12, 18, 29, 255);
  c.text(24, 20, `Gaze: ${gaze.status} | correction: ${gaze.calibrationStatus}`,
    230, 240, 255, 255, 24);
  if (gaze.valid && gaze.direction.z > 0) {
    const distance = -gaze.origin.z / gaze.direction.z;
    const x = (gaze.origin.x + distance * gaze.direction.x) / 1.1 * 1024 + 512;
    const y = 320 - (gaze.origin.y + distance * gaze.direction.y) / .6875 * 640;
    if (distance > 0) c.circle(x, y, 10, 80, 255, 130, 255, true);
  }
  for (const [index, side] of ['left', 'right'].entries()) {
    const hand = getFingerTracking(side), y = 110 + index * 110;
    c.text(24, y, `${side}: ${hand.valid ? 'available' : 'unavailable'}`,
      230, 240, 255, 255, 24);
    c.text(24, y + 40, `Ring ${value(hand.sensors.ring)} | pinky ${value(hand.sensors.pinky)} | grip ${value(hand.sensors.grip)}`,
      180, 210, 240, 255, 22);
  }
  c.present();
}
