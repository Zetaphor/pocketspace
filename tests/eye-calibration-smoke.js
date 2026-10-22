// Runtime file round-trip fixture, not a wearer calibration.
const identity={version:1,source:'openvr',model:'head-angular-affine',units:'radians',yaw:[0,1,0],pitch:[0,0,1],bounds:[-1.4,1.4,-1.4,1.4]};
const info=getEyeTrackingCalibration();
if(info.outputPath){
 for(const bad of [null,{}, {...identity,version:1.5},{...identity,yaw:[0,1]}, {...identity,bounds:[1,-1,-1,1]}, {...identity,pitch:[0,Infinity,1]}]){
  let rejected=false;try{saveEyeTrackingCalibration(bad);}catch(e){rejected=true;}if(!rejected)throw Error('invalid calibration accepted');
 }
 saveEyeTrackingCalibration(identity);if(!getEyeTrackingCalibration().loaded)throw Error('save did not activate calibration');}
else if(!info.loaded)throw Error('expected calibration loaded from file');
let logged=false;getEyeTracking();
function update(){
 const raw=getEyeTracking(),corrected=getCalibratedEyeTracking();
 if(raw.valid&&corrected.calibrationApplied){
  if(Math.hypot(raw.direction.x-corrected.direction.x,raw.direction.y-corrected.direction.y,raw.direction.z-corrected.direction.z)>1e-4)throw Error('identity correction changed ray');
  if(!logged){print('EYE_CALIBRATION_IDENTITY_PASS');logged=true;}
 }
}
print('EYE_CALIBRATION_FILE_PASS');
