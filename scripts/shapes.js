// Build a small table with a glowing lamp from generated shapes, then place it
// where its parts describe: at the returned center, scaled to the returned extent.
const wood = [.55, .35, .18];
const legs = [[-.16, -.16], [.16, -.16], [-.16, .16], [.16, .16]].map(([x, z]) =>
  ({shape: 'cylinder', position: [x, -.2, z], scale: [.035, .36, .035], color: wood}));
const thing = createShapeAsset([
  {shape: 'cube', position: [0, 0, 0], scale: [.42, .04, .42], color: wood, finish: 'glossy'},
  ...legs,
  {shape: 'cone', position: [0, .1, 0], scale: [.1, .16, .1], color: [.6, .6, .65], finish: 'metal'},
  {shape: 'sphere', position: [0, .2, 0], scale: [.07, .07, .07], color: [1, .8, .4], finish: 'glow'},
  {shape: 'torus', position: [0, .03, 0], scale: [.14, .015, .14], color: [.2, .4, .9], finish: 'metal'}
]);
const {x, y, z} = thing.center;
const table = createObject(thing.assetId, x, y, z, thing.extent);
let angle = 0;
function update(dt) {
  angle += dt * .3;
  setObjectYaw(table, angle);
}
