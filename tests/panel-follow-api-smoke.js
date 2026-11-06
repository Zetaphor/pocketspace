// Panels placed on objects: validation, removal, container disposal and snapshot remapping.
function check(v, message = 'panel follow assertion') { if (!v) throw Error(message); }
function fails(fn) { let caught = false; try { fn(); } catch (_) { caught = true; } check(caught, 'expected rejection'); }
const asset = loadModel('assets/samples/Box/Box.glb');
const owner = createSceneContainer();
const handle = createObject(asset, .2, .1, .4, .1, {container: owner});
const label = createPanel({widthPixels: 32, heightPixels: 16, widthMeters: .2, container: owner});
label.canvas.clear(40, 80, 120, 255); label.present();

// Invalid modes and options throw and leave the placement unchanged.
panelSetTransform(label, 'world', .5, .5, .5);
fails(() => panelSetTransform(label, 'sideways', 0, 0, 0));
fails(() => panelSetTransform(label, 'object', 0, 0, 0));
fails(() => panelSetTransform(label, 'object', 0, 0, 0, {}));
for (const objectId of [9999, 0, -1, 1.5, '1', null]) fails(() => panelSetTransform(label, 'object', 0, 0, 0, {objectId}));
fails(() => panelSetTransform(label, 'object', 0, 0, 0, {objectId: handle, viewerFacing: 1}));
fails(() => panelSetTransform(label, 'world', 0, 0, 0, {objectId: handle}));
fails(() => panelSetTransform(label, 'object', NaN, 0, 0, {objectId: handle}));
let s = captureSceneContainer(owner);
check(s.panels[0].mode === 'world' && s.panels[0].position.x === .5 && s.panels[0].objectId === null && s.panels[0].viewerFacing === false);

panelSetTransform(label, 'object', 0, .15, 0, {objectId: handle, viewerFacing: true});
s = captureSceneContainer(owner);
check(s.panels[0].mode === 'object' && s.panels[0].objectId === handle && s.panels[0].viewerFacing === true);
// Restores follow the restored object, in memory and from disk.
const copy = restoreSceneContainer(s);
let again = captureSceneContainer(copy.containerId);
check(again.panels[0].objectId === copy.objectMap[handle] && again.panels[0].objectId !== handle && again.panels[0].viewerFacing);
saveSceneContainer(owner, 'follow');
const loaded = loadSceneContainer('follow');
check(captureSceneContainer(loaded.containerId).panels[0].objectId === loaded.objectMap[handle]);
disposeSceneContainer(loaded.containerId);
function bad(edit) { const t = JSON.parse(JSON.stringify(s)); edit(t); fails(() => restoreSceneContainer(t)); }
bad(t => t.panels[0].objectId = 9999);
bad(t => t.panels[0].objectId = 'x');
bad(t => t.panels[0].viewerFacing = 1);
bad(t => delete t.panels[0].viewerFacing);
bad(t => delete t.panels[0].objectId);
bad(t => { t.panels[0].mode = 'world'; t.panels[0].viewerFacing = false; });
bad(t => { t.panels[0].objectId = null; t.panels[0].mode = 'world'; });

// A root panel following a contained object would leave a dangling reference.
const outside = createPanel({widthPixels: 8, heightPixels: 8, widthMeters: .1});
panelSetTransform(outside, 'object', 0, 0, 0, {objectId: copy.objectMap[handle]});
fails(() => captureSceneContainer(copy.containerId));
// Disposing the followed object's container detaches the panel and hides it.
disposeSceneContainer(copy.containerId);
globalThis.outsideId = outside.id;

// Removing the object keeps the panel placed but detached.
const loose = createObject(asset, 0, 0, 0, .1, {container: owner});
const tag = createPanel({widthPixels: 8, heightPixels: 8, widthMeters: .1, container: owner});
panelSetTransform(tag, 'object', 0, 0, .1, {objectId: loose});
removeObject(loose);
s = captureSceneContainer(owner);
check(s.panels[1].mode === 'object' && s.panels[1].objectId === null);
check(captureSceneContainer(restoreSceneContainer(s).containerId).panels[1].objectId === null);
// Placing it again attaches to a new object.
panelSetTransform(tag, 'object', 0, 0, .1, {objectId: handle});
globalThis.tagId = tag.id;
