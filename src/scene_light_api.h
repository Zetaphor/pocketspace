#pragma once
static bool pointLightId(JSContext *ctx,JSValueConst value,int &id){
 double n=0;if(!JS_IsNumber(value)||JS_ToFloat64(ctx,&n,value)<0||!std::isfinite(n)||n<1||n>=INT_MAX||n!=std::floor(n)){JS_ThrowRangeError(ctx,"light ID must be a positive integer");return false;}id=int(n);return true;
}
static bool readSpotShadow(JSContext *ctx,JSValueConst options,PointLight &candidate){
 JSValue value=JS_GetPropertyStr(ctx,options,"shadow");if(JS_IsException(value))return false;
 ScopeExit release{[&]{JS_FreeValue(ctx,value);}};
 if(JS_IsUndefined(value))return true;
 if(!candidate.spot){JS_ThrowTypeError(ctx,"only spotlights support shadow");return false;}
 if(!JS_IsObject(value)||JS_IsNull(value)){JS_ThrowTypeError(ctx,"shadow must be an options object");return false;}
 JSValue enabled=JS_GetPropertyStr(ctx,value,"enabled");if(JS_IsException(enabled))return false;
 if(!JS_IsUndefined(enabled)){if(!JS_IsBool(enabled)){JS_FreeValue(ctx,enabled);JS_ThrowTypeError(ctx,"shadow.enabled must be boolean");return false;}candidate.shadow.enabled=JS_ToBool(ctx,enabled)>0;}JS_FreeValue(ctx,enabled);
 for(auto [name,destination,lo,hi]:{std::tuple{"near",&candidate.shadow.nearPlane,.001f,1.f},std::tuple{"bias",&candidate.shadow.bias,0.f,.05f},std::tuple{"normalBias",&candidate.shadow.normalBias,0.f,.1f},std::tuple{"maxGpuMs",&candidate.shadow.maxGpuMs,.001f,16.f}}){
  JSValue item=JS_GetPropertyStr(ctx,value,name);if(JS_IsException(item))return false;
  const bool missing=JS_IsUndefined(item);float n=0;const bool valid=missing||(JS_IsNumber(item)&&number(ctx,item,n)&&n>=lo&&n<=hi);JS_FreeValue(ctx,item);
  if(!valid){JS_ThrowRangeError(ctx,"invalid shadow.%s",name);return false;}if(!missing)*destination=n;
 }
 for(auto [name,destination,lo,hi]:{std::tuple{"resolution",&candidate.shadow.resolution,256,1024},std::tuple{"maxTriangles",&candidate.shadow.maxTriangles,1,1000000}}){
  JSValue item=JS_GetPropertyStr(ctx,value,name);if(JS_IsException(item))return false;
  const bool missing=JS_IsUndefined(item);double n=0;const bool valid=missing||(JS_IsNumber(item)&&JS_ToFloat64(ctx,&n,item)==0&&std::isfinite(n)&&n==std::floor(n)&&n>=lo&&n<=hi&&(std::string(name)!="resolution"||(int(n)&(int(n)-1))==0));JS_FreeValue(ctx,item);
  if(!valid){JS_ThrowRangeError(ctx,"invalid shadow.%s",name);return false;}if(!missing)*destination=int(n);
 }
 ++candidate.shadowRevision;return true;
}
static bool availableShadowSlot(const PointLight &candidate){
 if(!candidate.spot||!candidate.enabled||!candidate.shadow.enabled)return true;
 return std::none_of(currentScene->lights.begin(),currentScene->lights.end(),[&](const PointLight &other){return other.id!=candidate.id&&other.spot&&other.enabled&&other.shadow.enabled;});
}
static bool readPointLight(JSContext *ctx,JSValueConst options,PointLight &candidate){
 if(!JS_IsObject(options)||JS_IsNull(options)){JS_ThrowTypeError(ctx,"light options must be an object");return false;}
 if(candidate.directional)for(const char *name:{"position","range","innerConeAngle","outerConeAngle"}){
  JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return false;
  const bool supplied=!JS_IsUndefined(value);JS_FreeValue(ctx,value);
  if(supplied){JS_ThrowTypeError(ctx,"directional lights do not support %s",name);return false;}
 }
 for(auto [name,destination,low,high]:{std::tuple{"intensity",&candidate.intensity,0.f,64.f},std::tuple{"range",&candidate.range,.001f,64.f}}){
  if(candidate.directional&&std::string(name)=="range")continue;
  JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return false;
  const bool missing=JS_IsUndefined(value);float n=0;const bool valid=missing||(JS_IsNumber(value)&&number(ctx,value,n)&&n>=low&&n<=high);JS_FreeValue(ctx,value);
  if(!valid){JS_ThrowRangeError(ctx,"invalid point light %s",name);return false;}if(!missing)*destination=n;
 }
 for(auto [name,destination,low,high]:{std::tuple{"position",&candidate.position,-10000.f,10000.f},std::tuple{"color",&candidate.color,0.f,64.f}}){
  if(candidate.directional&&std::string(name)=="position")continue;
  JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return false;
  if(JS_IsUndefined(value)){JS_FreeValue(ctx,value);continue;}
  JSValue count=JS_GetPropertyStr(ctx,value,"length");double n=0;bool valid=JS_IsArray(value)&&JS_IsNumber(count)&&JS_ToFloat64(ctx,&n,count)==0&&n==3;JS_FreeValue(ctx,count);
  for(int c=0;c<3&&valid;c++){JSValue component=JS_GetPropertyUint32(ctx,value,c);float v=0;valid=JS_IsNumber(component)&&number(ctx,component,v)&&v>=low&&v<=high;JS_FreeValue(ctx,component);if(valid)(*destination)[c]=v;}
  JS_FreeValue(ctx,value);if(JS_HasException(ctx))return false;
  if(!valid){JS_ThrowRangeError(ctx,"point light %s requires three finite numbers",name);return false;}
 }
 JSValue enabled=JS_GetPropertyStr(ctx,options,"enabled");if(JS_IsException(enabled))return false;
 if(!JS_IsUndefined(enabled)){if(!JS_IsBool(enabled)){JS_FreeValue(ctx,enabled);JS_ThrowTypeError(ctx,"enabled must be boolean");return false;}candidate.enabled=JS_ToBool(ctx,enabled)>0;}JS_FreeValue(ctx,enabled);
 JSValue object=JS_GetPropertyStr(ctx,options,"objectId");if(JS_IsException(object))return false;
 if(JS_IsNull(object))candidate.objectId=0;
 else if(!JS_IsUndefined(object)&&!pointLightId(ctx,object,candidate.objectId)){JS_FreeValue(ctx,object);return false;}JS_FreeValue(ctx,object);
 JSValue owner=JS_GetPropertyStr(ctx,options,"container");if(JS_IsException(owner))return false;
 if(!JS_IsUndefined(owner)&&!containerId(ctx,owner,candidate.container)){JS_FreeValue(ctx,owner);return false;}JS_FreeValue(ctx,owner);
 if(candidate.spot||candidate.directional){
  JSValue direction=JS_GetPropertyStr(ctx,options,"direction");if(JS_IsException(direction))return false;
  if(!JS_IsUndefined(direction)){
   JSValue count=JS_GetPropertyStr(ctx,direction,"length");double n=0;bool valid=JS_IsArray(direction)&&JS_IsNumber(count)&&JS_ToFloat64(ctx,&n,count)==0&&n==3;JS_FreeValue(ctx,count);
   for(int c=0;c<3&&valid;c++){JSValue component=JS_GetPropertyUint32(ctx,direction,c);float v=0;valid=JS_IsNumber(component)&&number(ctx,component,v)&&v>=-64&&v<=64;JS_FreeValue(ctx,component);if(valid)candidate.direction[c]=v;}
   JS_FreeValue(ctx,direction);if(JS_HasException(ctx))return false;
   if(!valid){JS_ThrowRangeError(ctx,"direction requires three finite numbers from -64 to 64");return false;}
  }else JS_FreeValue(ctx,direction);
  if(candidate.spot)for(auto [name,destination,low]:{std::tuple{"innerConeAngle",&candidate.innerConeAngle,0.f},std::tuple{"outerConeAngle",&candidate.outerConeAngle,.001f}}){
   JSValue value=JS_GetPropertyStr(ctx,options,name);if(JS_IsException(value))return false;
   const bool missing=JS_IsUndefined(value);float n=0;const bool valid=missing||(JS_IsNumber(value)&&number(ctx,value,n)&&n>=low&&n<=float(std::acos(-1)/2));JS_FreeValue(ctx,value);
   if(!valid){JS_ThrowRangeError(ctx,"invalid spotlight %s",name);return false;}if(!missing)*destination=n;
  }
  double length=0;for(float v:candidate.direction)length+=double(v)*v;
  if(length<1e-12||(candidate.spot&&candidate.innerConeAngle>candidate.outerConeAngle)){JS_ThrowRangeError(ctx,"light requires nonzero direction and valid cone angles");return false;}
 }
 if(!readSpotShadow(ctx,options,candidate))return false;
 if(candidate.spot&&candidate.shadow.enabled&&(candidate.shadow.nearPlane>=candidate.range||candidate.outerConeAngle>1.5f)){JS_ThrowRangeError(ctx,"shadow requires near < range and outerConeAngle <= 1.5 radians");return false;}
 // Revalidate after every script getter has run; none of the copied IDs hold pointers.
 if(candidate.container&&!currentScene->containers.count(candidate.container)){JS_ThrowRangeError(ctx,"light container was disposed");return false;}
 if(candidate.objectId&&!findObject(candidate.objectId)){JS_ThrowRangeError(ctx,"unknown light attachment object");return false;}
 return true;
}
static JSValue pointLightValue(JSContext *ctx,const PointLight &light){
 JSValue value=JS_NewObject(ctx);
 JS_SetPropertyStr(ctx,value,"type",JS_NewString(ctx,light.directional?"directional":light.spot?"spot":"point"));
 if(light.spot||light.directional){JSValue direction=JS_NewArray(ctx);for(int c=0;c<3;c++)JS_SetPropertyUint32(ctx,direction,c,JS_NewFloat64(ctx,light.direction[c]));JS_SetPropertyStr(ctx,value,"direction",direction);}
 if(light.spot){JSValue shadow=JS_NewObject(ctx);JS_SetPropertyStr(ctx,shadow,"enabled",JS_NewBool(ctx,light.shadow.enabled));
  JS_SetPropertyStr(ctx,shadow,"resolution",JS_NewInt32(ctx,light.shadow.resolution));JS_SetPropertyStr(ctx,shadow,"maxTriangles",JS_NewInt32(ctx,light.shadow.maxTriangles));
  for(auto [name,n]:{std::pair{"near",light.shadow.nearPlane},{"bias",light.shadow.bias},{"normalBias",light.shadow.normalBias},{"maxGpuMs",light.shadow.maxGpuMs}})JS_SetPropertyStr(ctx,shadow,name,JS_NewFloat64(ctx,n));
  JS_SetPropertyStr(ctx,value,"shadow",shadow);
 }
 if(light.spot){JS_SetPropertyStr(ctx,value,"innerConeAngle",JS_NewFloat64(ctx,light.innerConeAngle));JS_SetPropertyStr(ctx,value,"outerConeAngle",JS_NewFloat64(ctx,light.outerConeAngle));}
 for(auto [name,n]:{std::pair{"id",light.id},{"container",light.container}})JS_SetPropertyStr(ctx,value,name,JS_NewInt32(ctx,n));
 JS_SetPropertyStr(ctx,value,"objectId",light.objectId?JS_NewInt32(ctx,light.objectId):JS_NULL);
 for(auto [name,vector]:{std::pair{"position",light.position},{"color",light.color}}){if(light.directional&&std::string(name)=="position")continue;JSValue array=JS_NewArray(ctx);for(int c=0;c<3;c++)JS_SetPropertyUint32(ctx,array,c,JS_NewFloat64(ctx,vector[c]));JS_SetPropertyStr(ctx,value,name,array);}
 JS_SetPropertyStr(ctx,value,"intensity",JS_NewFloat64(ctx,light.intensity));if(!light.directional)JS_SetPropertyStr(ctx,value,"range",JS_NewFloat64(ctx,light.range));JS_SetPropertyStr(ctx,value,"enabled",JS_NewBool(ctx,light.enabled));return value;
}
static JSValue createSceneLight(JSContext *ctx,int argc,JSValueConst *argv,bool spot,bool directional=false){
 PointLight candidate;candidate.spot=spot;candidate.directional=directional;JSValue options=argc?JS_DupValue(ctx,argv[0]):JS_NewObject(ctx);const bool valid=readPointLight(ctx,options,candidate);JS_FreeValue(ctx,options);if(!valid)return JS_EXCEPTION;
 if(currentScene->lights.size()>=maxPointLights||currentScene->nextLightId==INT_MAX)return JS_ThrowRangeError(ctx,"scene light limit reached (8)");
 if(!availableShadowSlot(candidate))return JS_ThrowRangeError(ctx,"only one enabled shadow-casting spotlight is supported");
 candidate.id=currentScene->nextLightId;currentScene->lights.push_back(candidate);++currentScene->nextLightId;return JS_NewInt32(ctx,candidate.id);
}
static JSValue setSceneLight(JSContext *ctx,int argc,JSValueConst *argv,bool spot,bool directional=false){
 int id=0;if(argc<2)return JS_ThrowTypeError(ctx,"set light(id, options)");if(!pointLightId(ctx,argv[0],id))return JS_EXCEPTION;
 auto *light=findSceneItem(currentScene->lights,id);if(!light||light->spot!=spot||light->directional!=directional)return JS_ThrowRangeError(ctx,"unknown light ID or wrong light type");auto candidate=*light;
 if(!readPointLight(ctx,argv[1],candidate))return JS_EXCEPTION;
 if(!availableShadowSlot(candidate))return JS_ThrowRangeError(ctx,"only one enabled shadow-casting spotlight is supported");
 light=findSceneItem(currentScene->lights,id);if(!light)return JS_ThrowRangeError(ctx,"light removed during option evaluation");*light=candidate;return JS_UNDEFINED;
}
static JSValue getSceneLight(JSContext *ctx,int argc,JSValueConst *argv,bool spot,bool directional=false){
 int id=0;if(!argc)return JS_ThrowTypeError(ctx,"get light(id)");if(!pointLightId(ctx,argv[0],id))return JS_EXCEPTION;
 const auto *light=findSceneItem(currentScene->lights,id);if(!light||light->spot!=spot||light->directional!=directional)return JS_ThrowRangeError(ctx,"unknown light ID or wrong light type");return pointLightValue(ctx,*light);
}
static JSValue removeSceneLight(JSContext *ctx,int argc,JSValueConst *argv,bool spot,bool directional=false){
 int id=0;if(!argc)return JS_ThrowTypeError(ctx,"remove light(id)");if(!pointLightId(ctx,argv[0],id))return JS_EXCEPTION;
 const auto *light=findSceneItem(currentScene->lights,id);if(!light||light->spot!=spot||light->directional!=directional)return JS_ThrowRangeError(ctx,"unknown light ID or wrong light type");std::erase_if(currentScene->lights,[&](const PointLight &light){return light.id==id;});return JS_UNDEFINED;
}

static JSValue jsCreatePointLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return createSceneLight(ctx,argc,argv,false);}
static JSValue jsSetPointLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return setSceneLight(ctx,argc,argv,false);}
static JSValue jsGetPointLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return getSceneLight(ctx,argc,argv,false);}
static JSValue jsRemovePointLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return removeSceneLight(ctx,argc,argv,false);}
static JSValue jsCreateSpotLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return createSceneLight(ctx,argc,argv,true);}
static JSValue jsSetSpotLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return setSceneLight(ctx,argc,argv,true);}
static JSValue jsGetSpotLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return getSceneLight(ctx,argc,argv,true);}
static JSValue jsRemoveSpotLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return removeSceneLight(ctx,argc,argv,true);}

static JSValue jsCreateDirectionalLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return createSceneLight(ctx,argc,argv,false,true);}
static JSValue jsSetDirectionalLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return setSceneLight(ctx,argc,argv,false,true);}
static JSValue jsGetDirectionalLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return getSceneLight(ctx,argc,argv,false,true);}
static JSValue jsRemoveDirectionalLight(JSContext *ctx,JSValueConst,int argc,JSValueConst *argv){return removeSceneLight(ctx,argc,argv,false,true);}
