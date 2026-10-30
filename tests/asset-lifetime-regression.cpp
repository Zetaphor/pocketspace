// Actual GPU deletion, stable asset IDs and nested ownership/save rollback.
#define SF_OCCLUSION_HELPERS_ONLY
#include "render-occlusion-regression.cpp"
int main(){
 EGLDisplay display;EGLContext context;EGLSurface surface;assert(initEgl(display,context,surface));
 const auto dir=std::filesystem::temp_directory_path()/("ps-assets-"+std::to_string(getpid()));std::filesystem::create_directory(dir);scenePersistence::directory=dir.string();ScopeExit cleanup{[&]{std::filesystem::remove_all(dir);}};
 {
 ScriptHost host;assert(loadScript(host,"tests/container-hierarchy-api-smoke.js"));
 const auto parents=host.scene.containerParents;const auto owners=host.scene.containers;const int ownerNext=host.scene.nextContainerId,objectNext=host.scene.nextId;currentScene=liveScene=&host.scene;
 reserveObjectOverlay=[](int){return std::string("EXPECTED hierarchy reservation failure");};
 auto failed=JS_Eval(host.ctx,"restoreSceneContainer(snapshot)",strlen("restoreSceneContainer(snapshot)"),"hierarchy-rollback",JS_EVAL_TYPE_GLOBAL);assert(JS_IsException(failed));JS_FreeValue(host.ctx,failed);auto error=JS_GetException(host.ctx);JS_FreeValue(host.ctx,error);reserveObjectOverlay={};liveScene=nullptr;
 assert(host.scene.containerParents==parents&&host.scene.containers==owners&&host.scene.nextContainerId==ownerNext&&host.scene.nextId==objectNext);closeScript(host);
 const auto path=dir/"assets.js";
 std::ofstream(path)<<R"JS(
 function check(v){if(!v)throw Error('asset assertion');}
 function fails(fn){let caught=false;try{fn()}catch(e){caught=true}check(caught);}
 const owner=createSceneContainer(),a=loadModel('assets/samples/Box/Box.glb',512,{pbr:true,optimizeVertexCache:true});
 const object=createObject(a,0,0,0,.2,{container:owner});setObjectPhysics(object,{shape:'convex'});
 const snapshot=captureSceneContainer(owner);fails(()=>unloadModel(a));
 const hidden=createObject(a,0,0,0,.1);setObjectVisible(hidden,false);disposeSceneContainer(owner);fails(()=>unloadModel(a));removeObject(hidden);
 )JS";
 assert(loadScript(host,path.string()));auto eval=[&](const char *source){auto value=JS_Eval(host.ctx,source,strlen(source),"asset-test",JS_EVAL_TYPE_GLOBAL);if(JS_IsException(value)){auto e=JS_GetException(host.ctx);const char *message=JS_ToCString(host.ctx,e);fprintf(stderr,"%s\n",message?message:"exception");if(message)JS_FreeCString(host.ctx,message);JS_FreeValue(host.ctx,e);assert(false);}JS_FreeValue(host.ctx,value);};
 assert(host.scene.physics->hasConvexHull(1));auto &asset=host.scene.assets[0];uploadModel(asset);assert(asset.uploaded);GLuint vbo=asset.primitives[0].vbo,texture=asset.primitives[0].texture,surfaceVbo=asset.primitives[0].surfaceVbo,index=asset.primitives[0].indexBuffer;assert(glIsBuffer(vbo)&&glIsTexture(texture));
 eval("unloadModel(a);fails(()=>unloadModel(a));fails(()=>getAnimations(a));fails(()=>setModelLighting(a,{}));fails(()=>createObject(a,0,0,0,.1));for(const bad of [0,-1,1.5,'1',null,Infinity])fails(()=>unloadModel(bad));");
 assert(!glIsBuffer(vbo)&&!glIsTexture(texture)&&(!surfaceVbo||!glIsBuffer(surfaceVbo))&&(!index||!glIsBuffer(index)));assert(!host.scene.physics->hasConvexHull(1)&&!loadedAsset(host.scene,1)&&loadedAssetCount(host.scene)==0&&host.scene.assets[0].primitives.empty());
 eval(R"JS(
 const b=loadModel('assets/samples/Box/Box.glb',768);check(b>a);check(loadModel('assets/samples/Box/Box.glb')===b);unloadModel(b);
 const restored=restoreSceneContainer(snapshot);check(restored.objectIds.length===1);disposeSceneContainer(restored.containerId);
 const c=loadModel('assets/samples/Box/Box.glb');check(c>b);unloadModel(c);
 for(let i=0;i<40;i++){const id=loadModel('assets/samples/Box/Box.glb');check(id>c);unloadModel(id);}
 const getter=loadModel('assets/samples/Box/Box.glb');fails(()=>createObject(getter,0,0,0,.1,{get container(){unloadModel(getter);return 0;}}));
 const lit=loadModel('assets/samples/Box/Box.glb',512,{pbr:true});fails(()=>setModelLighting(lit,{get intensity(){unloadModel(lit);return 1;}}));
 const marker=loadModel('assets/samples/Box/Box.glb');
 )JS");
 // Uploaded PBR LOD/environment payload and skeletal users follow the same guard.
 eval("globalThis.carAsset=loadModel('assets/samples/ToyCar/ToyCar.glb',512,{pbr:true,optimizeVertexCache:true});setModelLighting(carAsset,{environment:'assets/environments/studio.psenv'});");
 const int carId=int(host.scene.assets.size());auto &car=host.scene.assets.back();uploadModel(car);const auto budgetBefore=pbrLodBudget.used;std::weak_ptr<LightingEnvironment> environment=car.environment;std::vector<GLuint> carBuffers;
 for(const auto &part:car.primitives){carBuffers.push_back(part.vbo);carBuffers.push_back(part.surfaceVbo);if(part.indexBuffer)carBuffers.push_back(part.indexBuffer);for(const auto &lod:part.lod){if(lod.indexBuffer)carBuffers.push_back(lod.indexBuffer);if(lod.vbo)carBuffers.push_back(lod.vbo);if(lod.surfaceVbo)carBuffers.push_back(lod.surfaceVbo);}}
 eval("unloadModel(carAsset)");assert(!loadedAsset(host.scene,carId)&&environment.expired()&&pbrLodBudget.used<=budgetBefore);for(GLuint buffer:carBuffers)if(buffer)assert(!glIsBuffer(buffer));
 eval("const fox=loadModel('assets/samples/Fox/Fox.glb');const animated=createObject(fox,0,0,0,.2);playAnimation(animated,0);fails(()=>unloadModel(fox));removeObject(animated);unloadModel(fox);");
 assert(loadedAssetCount(host.scene)==1);auto *active=host.ctx;const auto slots=host.scene.assets.size();std::ofstream(path)<<"unloadModel(loadModel('assets/samples/Box/Box.glb'));throw Error('EXPECTED asset rollback');";assert(!loadScript(host,path.string())&&host.ctx==active&&host.scene.assets.size()==slots&&loadedAssetCount(host.scene)==1);closeScript(host);
 }
 {
  // Distinct paths prove the limit counts live models, while repeated cache hits
  // remain usable even when all 32 live slots are occupied.
  for(int i=0;i<33;i++)std::filesystem::copy_file("assets/samples/Box/Box.glb",dir/("model-"+std::to_string(i)+".glb"));
  const auto path=dir/"capacity.js";std::ofstream script(path);
  script<<"function check(v){if(!v)throw Error('asset capacity');}function fails(f){let caught=false;try{f()}catch(e){caught=true}check(caught);}";
  script<<"const paths=";for(int i=0;i<33;i++)script<<(i?",":"[")<<"'"<<(dir/("model-"+std::to_string(i)+".glb")).string()<<"'";script<<"];const ids=paths.slice(0,32).map(p=>loadModel(p));check(loadModel(paths[0])===ids[0]);fails(()=>loadModel(paths[32]));unloadModel(ids[0]);const extra=loadModel(paths[32]);check(extra>ids[31]);const owner=createSceneContainer();createObject(ids[1],0,0,0,.1,{container:owner});const snap=captureSceneContainer(owner);snap.models[0].path=paths[0];fails(()=>restoreSceneContainer(snap));unloadModel(extra);const restored=restoreSceneContainer(snap);check(restored.objectIds.length===1);disposeSceneContainer(restored.containerId);disposeSceneContainer(owner);for(const id of ids.slice(1))unloadModel(id);";script.close();
  ScriptHost host;assert(loadScript(host,path.string()));assert(loadedAssetCount(host.scene)==1);closeScript(host);
 }
 assert(pbrLodBudget.used==0&&glGetError()==GL_NO_ERROR);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroySurface(display,surface);eglDestroyContext(display,context);eglTerminate(display);puts("PASS nested containers/subtree snapshots and asset lifetime: GPU/physics cleanup, invalid IDs, getter/reload rollback and repeated release/reload");
}
