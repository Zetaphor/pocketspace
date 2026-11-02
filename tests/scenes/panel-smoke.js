const panel = createPanel({widthPixels: 1024, heightPixels: 512, widthMeters: 1.2});
panel.canvas.clear(12, 18, 29, 255);
panel.canvas.text(28, 24, 'Steam Frame controller debug', 235, 245, 255, 255, 32);
panel.canvas.text(28, 70, 'Input API online', 130, 220, 160, 255, 26);
panel.present();
function update() {
  const left = getControllerInput('left');
  panel.canvas.clear(12, 18, 29, 255);
  panel.canvas.text(28, 24, 'Steam Frame controller debug', 235, 245, 255, 255, 32);
  panel.canvas.text(28, 72, `left valid: ${left.valid}`, 235, 245, 255, 255, 26);
  panel.canvas.text(28, 112, `trigger: ${left.analog.trigger.toFixed(2)}`, 235, 245, 255, 255, 26);
  panel.canvas.text(28, 152, `grip: ${left.analog.grip.toFixed(2)}`, 235, 245, 255, 255, 26);
  panel.canvas.text(28, 192, `stick: ${left.analog.thumbstick.x.toFixed(2)}, ${left.analog.thumbstick.y.toFixed(2)}`, 235, 245, 255, 255, 26);
  panel.present();
}
