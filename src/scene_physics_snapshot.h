#pragma once
// Included inside scenePersistence after the strict JSON field helpers.
static constexpr double floatLimit=std::numeric_limits<float>::max();
static JSValue vectorValue(JSContext *ctx,physics::Vec3 v){
  require(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z),"non-finite physics vector");
  JSValue value=JS_NewObject(ctx);put(ctx,value,"x",JS_NewFloat64(ctx,v.x));put(ctx,value,"y",JS_NewFloat64(ctx,v.y));put(ctx,value,"z",JS_NewFloat64(ctx,v.z));return value;
}
static physics::Vec3 vectorRead(JSContext *ctx,JSValueConst value){return {float(number(ctx,value,"x",-floatLimit,floatLimit)),float(number(ctx,value,"y",-floatLimit,floatLimit)),float(number(ctx,value,"z",-floatLimit,floatLimit))};}
static JSValue optionalVector(JSContext *ctx,const std::optional<physics::Vec3> &v){return v?vectorValue(ctx,*v):JS_NULL;}
static std::optional<physics::Vec3> optionalVectorRead(JSContext *ctx,JSValueConst value,const char *key){auto v=field(ctx,value,key);if(JS_IsNull(v))return {};return vectorRead(ctx,v);}
static JSValue optionalNumber(JSContext *ctx,const std::optional<float> &v){return v?JS_NewFloat64(ctx,*v):JS_NULL;}
static std::optional<float> optionalNumberRead(JSContext *ctx,JSValueConst value,const char *key){auto v=field(ctx,value,key);if(JS_IsNull(v))return {};return float(number(ctx,value,key,-floatLimit,floatLimit));}
static std::string textField(JSContext *ctx,JSValueConst value,const char *key){auto v=field(ctx,value,key);return string(ctx,v);}
static JSValue poseValue(JSContext *ctx,const physics::Pose &p){
  Value value(ctx,JS_NewObject(ctx));put(ctx,value,"position",vectorValue(ctx,p.position));
  for(auto [key,n]:{std::pair{"pitch",p.pitch},std::pair{"yaw",p.yaw},std::pair{"roll",p.roll}}){require(std::isfinite(n),"non-finite reference rotation");put(ctx,value,key,JS_NewFloat64(ctx,n));}return JS_DupValue(ctx,value);
}
static physics::Pose poseRead(JSContext *ctx,JSValueConst value){auto p=field(ctx,value,"position");return {vectorRead(ctx,p),float(number(ctx,value,"pitch",-floatLimit,floatLimit)),float(number(ctx,value,"yaw",-floatLimit,floatLimit)),float(number(ctx,value,"roll",-floatLimit,floatLimit))};}
static constexpr std::pair<const char*,float physics::Settings::*> bodyFields[]={
  {"mass",&physics::Settings::mass},{"friction",&physics::Settings::friction},{"restitution",&physics::Settings::restitution},
  {"linearDamping",&physics::Settings::linearDamping},{"angularDamping",&physics::Settings::angularDamping},{"gravityFactor",&physics::Settings::gravityFactor}};
static JSValue bodyValue(JSContext *ctx,const physics::State &state){
  Value value(ctx,JS_NewObject(ctx)),settings(ctx,JS_NewObject(ctx));const auto &s=state.settings;
  put(ctx,settings,"type",JS_NewString(ctx,s.type==physics::Type::Static?"static":s.type==physics::Type::Kinematic?"kinematic":"dynamic"));
  put(ctx,settings,"shape",JS_NewString(ctx,s.shape==physics::Shape::Sphere?"sphere":s.shape==physics::Shape::Convex?"convex":s.shape==physics::Shape::Mesh?"mesh":"box"));
  put(ctx,settings,"continuous",JS_NewBool(ctx,s.continuous));for(auto [key,member]:bodyFields)put(ctx,settings,key,JS_NewFloat64(ctx,s.*member));
  put(ctx,settings,"halfExtents",optionalVector(ctx,s.halfExtents));put(ctx,settings,"offset",optionalVector(ctx,s.offset));put(ctx,settings,"radius",optionalNumber(ctx,s.radius));
  put(ctx,value,"settings",JS_DupValue(ctx,settings));put(ctx,value,"velocity",vectorValue(ctx,state.velocity));put(ctx,value,"angularVelocity",vectorValue(ctx,state.angularVelocity));return JS_DupValue(ctx,value);
}
static physics::State bodyRead(JSContext *ctx,JSValueConst value,const Object &o){
  physics::State state{};auto settings=field(ctx,value,"settings");auto &s=state.settings;
  const auto type=textField(ctx,settings,"type"),shape=textField(ctx,settings,"shape");
  require(type=="static"||type=="dynamic"||type=="kinematic","invalid physics type");s.type=type=="static"?physics::Type::Static:type=="kinematic"?physics::Type::Kinematic:physics::Type::Dynamic;
  require(shape=="box"||shape=="sphere"||shape=="convex"||shape=="mesh","invalid physics shape");s.shape=shape=="sphere"?physics::Shape::Sphere:shape=="convex"?physics::Shape::Convex:shape=="mesh"?physics::Shape::Mesh:physics::Shape::Box;
  s.continuous=boolean(ctx,settings,"continuous");for(auto [key,member]:bodyFields)s.*member=float(number(ctx,settings,key,-floatLimit,floatLimit));
  s.halfExtents=optionalVectorRead(ctx,settings,"halfExtents");s.offset=optionalVectorRead(ctx,settings,"offset");s.radius=optionalNumberRead(ctx,settings,"radius");
  auto v=field(ctx,value,"velocity"),a=field(ctx,value,"angularVelocity");state.velocity=vectorRead(ctx,v);state.angularVelocity=vectorRead(ctx,a);state.pose=physicsPose(o);return state;
}
static constexpr std::pair<const char*,float physics::JointSettings::*> jointFields[]={
  {"minAngle",&physics::JointSettings::minAngle},{"maxAngle",&physics::JointSettings::maxAngle},{"minDistance",&physics::JointSettings::minDistance}};
static constexpr std::pair<const char*,float physics::JointMotor::*> motorFields[]={
  {"targetAngle",&physics::JointMotor::targetAngle},{"targetVelocity",&physics::JointMotor::targetVelocity},{"maxTorque",&physics::JointMotor::maxTorque},
  {"frequency",&physics::JointMotor::frequency},{"damping",&physics::JointMotor::damping}};
static JSValue jointValue(JSContext *ctx,const physics::JointSnapshot &j){
  Value value(ctx,JS_NewObject(ctx)),settings(ctx,JS_NewObject(ctx)),motor(ctx,JS_NewObject(ctx));const auto &s=j.state.settings;
  put(ctx,value,"id",JS_NewInt32(ctx,j.state.id));put(ctx,value,"objectA",JS_NewInt32(ctx,j.state.objectA));put(ctx,value,"objectB",JS_NewInt32(ctx,j.state.objectB));
  put(ctx,settings,"type",JS_NewString(ctx,s.type==physics::JointType::Fixed?"fixed":s.type==physics::JointType::Hinge?"hinge":"distance"));
  put(ctx,settings,"anchor",optionalVector(ctx,s.anchor));put(ctx,settings,"anchorA",optionalVector(ctx,s.anchorA));put(ctx,settings,"anchorB",optionalVector(ctx,s.anchorB));put(ctx,settings,"axis",vectorValue(ctx,s.axis));
  for(auto [key,member]:jointFields)put(ctx,settings,key,JS_NewFloat64(ctx,s.*member));
  put(ctx,settings,"maxDistance",optionalNumber(ctx,s.maxDistance));put(ctx,settings,"breakForce",optionalNumber(ctx,s.breakForce));put(ctx,settings,"breakTorque",optionalNumber(ctx,s.breakTorque));put(ctx,settings,"collideConnected",JS_NewBool(ctx,s.collideConnected));
  JSValue spring=JS_NULL;if(s.spring){spring=JS_NewObject(ctx);put(ctx,spring,"frequency",JS_NewFloat64(ctx,s.spring->frequency));put(ctx,spring,"damping",JS_NewFloat64(ctx,s.spring->damping));}put(ctx,settings,"spring",spring);
  put(ctx,motor,"mode",JS_NewString(ctx,j.state.motor.mode==physics::MotorMode::Off?"off":j.state.motor.mode==physics::MotorMode::Velocity?"velocity":"position"));
  for(auto [key,member]:motorFields)put(ctx,motor,key,JS_NewFloat64(ctx,j.state.motor.*member));
  put(ctx,value,"settings",JS_DupValue(ctx,settings));put(ctx,value,"motor",JS_DupValue(ctx,motor));put(ctx,value,"referenceA",poseValue(ctx,j.referenceA));put(ctx,value,"referenceB",poseValue(ctx,j.referenceB));return JS_DupValue(ctx,value);
}
static physics::JointSnapshot jointRead(JSContext *ctx,JSValueConst value,const std::map<int,int> &objects){
  physics::JointSnapshot j{};auto &s=j.state.settings;
  j.state.id=int(number(ctx,value,"id",1,INT_MAX-1,true));
  const int a=int(number(ctx,value,"objectA",1,INT_MAX-1,true)),b=int(number(ctx,value,"objectB",1,INT_MAX-1,true));
  require(a!=b&&objects.count(a)&&objects.count(b),"joint endpoints must name two saved objects with physics bodies");j.state.objectA=objects.at(a);j.state.objectB=objects.at(b);
  auto settings=field(ctx,value,"settings"),motor=field(ctx,value,"motor"),ra=field(ctx,value,"referenceA"),rb=field(ctx,value,"referenceB");
  j.referenceA=poseRead(ctx,ra);j.referenceB=poseRead(ctx,rb);
  const auto type=textField(ctx,settings,"type");require(type=="fixed"||type=="hinge"||type=="distance","invalid joint type");s.type=type=="fixed"?physics::JointType::Fixed:type=="hinge"?physics::JointType::Hinge:physics::JointType::Distance;
  s.anchor=optionalVectorRead(ctx,settings,"anchor");s.anchorA=optionalVectorRead(ctx,settings,"anchorA");s.anchorB=optionalVectorRead(ctx,settings,"anchorB");auto axis=field(ctx,settings,"axis");s.axis=vectorRead(ctx,axis);
  for(auto [key,member]:jointFields)s.*member=float(number(ctx,settings,key,-floatLimit,floatLimit));
  s.maxDistance=optionalNumberRead(ctx,settings,"maxDistance");s.breakForce=optionalNumberRead(ctx,settings,"breakForce");s.breakTorque=optionalNumberRead(ctx,settings,"breakTorque");s.collideConnected=boolean(ctx,settings,"collideConnected");
  auto spring=field(ctx,settings,"spring");if(!JS_IsNull(spring))s.spring=physics::JointSpring{float(number(ctx,spring,"frequency",-floatLimit,floatLimit)),float(number(ctx,spring,"damping",-floatLimit,floatLimit))};
  const auto mode=textField(ctx,motor,"mode");require(mode=="off"||mode=="position"||mode=="velocity","invalid motor mode");j.state.motor.mode=mode=="off"?physics::MotorMode::Off:mode=="position"?physics::MotorMode::Position:physics::MotorMode::Velocity;
  for(auto [key,member]:motorFields)j.state.motor.*member=float(number(ctx,motor,key,-floatLimit,floatLimit));return j;
}
