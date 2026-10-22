const panel = createPanel({widthPixels:1024,heightPixels:512,widthMeters:1.2});
panelSetTransform(panel, 'viewerFacing', 0, .2, -1);
panelSetVisible(panel, true);
panelClear(panel, 12, 18, 29, 255);
panelText(panel, 24, 24, 'Panel API', 235, 245, 255, 255, 32);
panelCanvasText(panel, 24, 72, 'Canvas alias online', 130, 220, 160, 255, 26);
panelPresent(panel);
function update() { panelPresent(panel); }
