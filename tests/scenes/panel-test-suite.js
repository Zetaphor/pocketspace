// Run in the headset. Aim at the large panel, then hover, grab, and move the model.
const dashboard = createPanel({widthPixels:1024,heightPixels:640,widthMeters:.9,interactive:true});
panelSetTransform(dashboard,'viewerFacing',0,0,0);
const modePanels = [
  ['world',-.32,-.4,0],
  ['headLocked',0,-.6,1.1],
  ['arbitrary',.32,-.4,0]
].map(([mode,x,y,z]) => {
  const p=createPanel({widthPixels:320,heightPixels:96,widthMeters:.28});
  panelSetTransform(p,mode,x,y,z);
  if(mode==='arbitrary') panelSetRotation(p,.978,0,.208,0,1,0,-.208,0,.978);
  p.canvas.clear(20,38,58,235);
  p.canvas.text(12,18,mode,255,255,255,255,24);
  p.canvas.present();
  return p;
});
const temporary=createPanel({widthPixels:64,heightPixels:32,widthMeters:.1});
temporary.canvas.clear(255,0,0,255);temporary.canvas.present();
const imagePath='assets/panel-icon.png';
const asset=loadModel('assets/Avocado.glb',768);
const object=createObject(asset,-.58,-.05,0,.24);
setObjectInteraction(object,{grabbable:true,directGrab:true,laserYaw:true,depthAdjust:true,
  handRotationAxes:{pitch:true,yaw:true,roll:true}});
setLaserVisible('left',true);setLaserVisible('right',true);
const callback={},queued={},buttonCallback={},buttonQueue={};
const watchedButtons=['/input/trigger/click','/input/bumper/click','/input/thumbstick/click','/input/grip/click'];
let lastEvent='none',lastDraw=-1,elapsed=0,removed=false,imageOk=false,firstDraw=true,buttonHits=0;
function count(map,key){map[key]=(map[key]||0)+1;}
function countButton(map,e){if(e.type==='controller.button'&&e.down&&watchedButtons.includes(e.control))count(map,`${e.hand}:${e.control}`);}
function presses(map,hand){return ['trigger','bumper','thumbstick'].map(name=>map[`${hand}:/input/${name}/click`]||0).join('/');}
onInputEvent(e=>{
  count(callback,e.type);countButton(buttonCallback,e);
  if(e.type!=='controller.value')lastEvent=`${e.type} ${e.hand} ${e.control} id=${e.objectId} x=${e.x.toFixed(0)} y=${e.y.toFixed(0)}`;
  if(e.type==='controller.button'&&e.down&&watchedButtons.includes(e.control))print(`button ${e.hand} ${e.control}`);
  if(e.type==='panel.pointerDown'){
    if(e.x>=700&&e.x<=1000&&e.y>=520&&e.y<=580)buttonHits++;
    print(`panel click x=${e.x.toFixed(0)} y=${e.y.toFixed(0)} hits=${buttonHits}`);
  }
});
function draw(){
  const l=getControllerInput('left'),r=getControllerInput('right');
  const c=dashboard.canvas;
  c.clear(8,17,31,245);
  const fontOk=c.text(24,14,'Steam Frame integration test',235,245,255,255,30);
  c.line(20,56,1004,56,50,130,185,255);
  c.text(24,66,`Controller poses: L ${getController('left').valid} / R ${getController('right').valid}`,195,230,255,255,20);
  c.text(24,94,`Raw input: L ${l.valid} / R ${r.valid}`,195,230,255,255,20);
  c.text(24,122,`L trigger ${l.analog.trigger.toFixed(2)} grip ${l.analog.grip.toFixed(2)} stick ${l.analog.thumbstick.x.toFixed(2)},${l.analog.thumbstick.y.toFixed(2)}`,225,240,255,255,18);
  c.text(24,148,`R trigger ${r.analog.trigger.toFixed(2)} grip ${r.analog.grip.toFixed(2)} stick ${r.analog.thumbstick.x.toFixed(2)},${r.analog.thumbstick.y.toFixed(2)}`,225,240,255,255,18);
  c.text(24,178,`L clicks T${+l.buttons.trigger.down} U${+l.buttons.bumper.down} S${+l.buttons.thumbstick.down} G${+l.buttons.grip.down}`,255,225,150,255,18);
  c.text(430,178,`press events C/Q T/U/S ${presses(buttonCallback,'left')} : ${presses(buttonQueue,'left')}`,170,235,190,255,16);
  c.text(24,202,`R clicks T${+r.buttons.trigger.down} U${+r.buttons.bumper.down} S${+r.buttons.thumbstick.down} G${+r.buttons.grip.down}`,255,225,150,255,18);
  c.text(430,202,`press events C/Q T/U/S ${presses(buttonCallback,'right')} : ${presses(buttonQueue,'right')}`,170,235,190,255,16);
  c.text(24,230,`Last: ${lastEvent}`,255,205,115,255,18);
  let y=254;
  for(const type of ['controller.button','controller.value','panel.hoverStart','panel.pointerDown','panel.pointerUp','overlay.surfaceHoverStart',
    'overlay.meshHoverStart','overlay.grabStart','overlay.grabMove','overlay.grabEnd','overlay.move','overlay.rotate']){
    c.text(24,y,`${type}: callback ${callback[type]||0} / queue ${queued[type]||0}`,170,235,190,255,16);y+=19;
  }
  c.rect(20,500,984,115,40,80,120,255,false);
  c.line(28,555,220,555,255,160,80,255);
  c.circle(264,555,25,80,210,255,255,true);
  c.circle(328,555,25,255,210,90,255,false);
  imageOk=c.image(370,520,imagePath);
  c.rect(700,520,300,60,35,135,90,255,true);
  c.text(735,535,`CLICK HERE  ${buttonHits}`,255,255,255,255,24);
  c.text(24,610,`Font ${fontOk?'PASS':'FAIL'}  Image ${imageOk?'PASS':'FAIL'}  Remove ${removed?'PASS':'pending'}`,230,230,255,255,17);
  c.present();
  if(firstDraw){print(`panel test draw: font=${fontOk} image=${imageOk} model=${object}`);firstDraw=false;}
}
function update(dt){
  elapsed+=dt;
  for(const e of pollInputEvents()){count(queued,e.type);countButton(buttonQueue,e);}
  if(!removed&&elapsed>2){removePanel(temporary);removed=true;print('panel test removal: complete');}
  if(elapsed-lastDraw>=.1){draw();lastDraw=elapsed;}
}
