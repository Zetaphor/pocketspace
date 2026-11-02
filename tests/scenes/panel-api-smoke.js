const panel = createPanel({widthPixels:1024,heightPixels:512,widthMeters:1.2});
panelSetTransform(panel, 'viewerFacing', 0, .2, -1);
panelSetVisible(panel, true);
panel.canvas.clear(12, 18, 29, 255);
panel.canvas.text(24, 24, 'Panel API', 235, 245, 255, 255, 32);
panel.canvas.text(24, 72, 'Canvas online', 130, 220, 160, 255, 26);
panel.present();
function update() { panel.present(); }
