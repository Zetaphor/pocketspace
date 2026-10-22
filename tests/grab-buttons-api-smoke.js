const asset=loadModel('assets/samples/Box/Box.glb',512);
const object=createObject(asset,0,0,0,.15);
function mustThrow(call){let threw=false;try{call();}catch(e){threw=true;}if(!threw)throw Error('invalid grabButton accepted');}
for(const grabButton of ['trigger','grip','both']){
  setObjectInteraction(object,{grabbable:true,grabButton});
  enableObjectInteraction(object,{grabButton});
}
for(const grabButton of ['other','',null,1,true,[],{}]){
  mustThrow(()=>setObjectInteraction(object,{grabbable:false,grabButton}));
  mustThrow(()=>enableObjectInteraction(object,{grabButton}));
}
for(const options of [null,1,'grip'])mustThrow(()=>enableObjectInteraction(object,options));
enableObjectInteraction(object);
enableObjectInteraction(object,undefined);
setObjectInteraction(object,{directGrab:false});
setObjectInteraction(object,{grabButton:'trigger'});
print('GRAB_BUTTON_API_PASS valid modes, invalid values, helper options and legacy calls');
