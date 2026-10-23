function check(value) { if (!value) throw Error('collision API assertion'); }
function rejects(callback) { let failed = false; try { callback(); } catch (_) { failed = true; } check(failed); }
rejects(() => setPhysicsCollisionEvents());
rejects(() => setPhysicsCollisionEvents(1));
rejects(() => setPhysicsCollisionEvents(true, {stay: 1}));
setPhysicsCollisionEvents(true, {stay: true});
const asset = loadModel('assets/samples/Box/Box.glb');
const floor = createObject(asset, 0, -1, 0, 1.2);
setObjectPhysics(floor, {type: 'static'});
const box = createObject(asset, 0, .5, 0, .2);
setObjectPhysics(box);
let callbackEvents = [];
onInputEvent(event => { if (event.type.startsWith('physics.')) callbackEvents.push(event); });
function verifyContacts() {
  const polled = pollInputEvents().filter(event => event.type.startsWith('physics.'));
  check(JSON.stringify(polled) === JSON.stringify(callbackEvents));
  check(polled.filter(event => event.type === 'physics.collisionStart').length === 1);
  check(polled.some(event => event.type === 'physics.collisionStay'));
  check(!polled.some(event => event.type === 'physics.collisionEnd'));
  for (const event of polled) {
    check(event.objectId === floor && event.otherObjectId === box);
    check(event.normal.y > .99 && Math.abs(event.point.y + .4) < .03);
    check(event.penetration >= 0 && Number.isFinite(event.relativeSpeed));
  }
  callbackEvents = [];
}
function verifyEnd() {
  const events = pollInputEvents();
  check(events.length === 1 && events[0].type === 'physics.collisionEnd');
  check(callbackEvents.length === 1 && JSON.stringify(callbackEvents) === JSON.stringify(events));
  callbackEvents = [];
}
