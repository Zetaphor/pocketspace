#include "render_harness.h"
#include "directional-light-reference.h"

Renderer::Renderer(int storagePixels):samplePixels(storagePixels),backingPixels(storagePixels){
    assert(backingPixels>0);const int pixels=backingPixels;
    for(int i=0;i<3;i++)anchor.m[i][i]=1;
    auto v=compile(GL_VERTEX_SHADER,modelVertexShader),f=compile(GL_FRAGMENT_SHADER,modelFragmentShader);
    program=glCreateProgram();glAttachShader(program,v);glAttachShader(program,f);glLinkProgram(program);
    GLint linked=0;glGetProgramiv(program,GL_LINK_STATUS,&linked);assert(linked);glDeleteShader(v);glDeleteShader(f);
    glUseProgram(program);glUniform1i(glGetUniformLocation(program,"material"),0);
    vector("right",1,0,0);vector("up",0,1,0);vector("forward",0,0,-1);
    glEnable(GL_DEPTH_TEST);glDepthFunc(GL_LESS);glEnableVertexAttribArray(0);glEnableVertexAttribArray(1);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenTextures(1,&color);glBindTexture(GL_TEXTURE_2D,color);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,2*pixels,pixels,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color,0);
    glGenRenderbuffers(1,&depth);glBindRenderbuffer(GL_RENDERBUFFER,depth);glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH_COMPONENT24,2*pixels,pixels);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_RENDERBUFFER,depth);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    for(const char *path:{"assets/physics/ThinPlatform.glb","assets/physics/ThinWallBlue.glb","assets/samples/Box/Box.glb"}){
      ModelAsset asset;assert(loadModelFile(path,asset));uploadModel(asset);scene.assets.push_back(std::move(asset));
    }
    scene.objects.push_back({1,1,0,-.43f,0,1.2f,0});
    scene.objects.push_back({2,2,-.6f,-.1f,0,.6f,0,0,float(std::acos(-1)/2)});
    scene.objects.push_back({3,3,0,-.32f,0,.16f,0});
  }

std::vector<unsigned char> Renderer::renderSet(const std::vector<int> &ids,physics::Vec3 headScene,int eyeIndex,int sharedDepth,bool readback,float gridScale,bool cull,int tileKey,int requestedPixels){
    assert(samplePixels>0&&samplePixels<=backingPixels);const int pixels=samplePixels;
    pbrDepthDraws=pbrColorDraws=pbrDepthTriangles=pbrColorTriangles=0;
    if(requestedPixels)gridScale*=surfaceCoverageWidth(1,pixels,requestedPixels,true);
    if(updateShadows)spotShadows.update(scene);
    const auto frameLights=resolvePointLights(scene);
    if(selectLocalLights)updateObjectLights(scene,frameLights,objectLightSets,selectConeLights);
    auto panel=anchor,head=anchor;
    for(int row=0;row<3;row++){
      panel.m[row][3]+=anchor.m[row][1]*-.2f;
      head.m[row][3]=anchor.m[row][3]+anchor.m[row][0]*headScene.x+anchor.m[row][1]*headScene.y-anchor.m[row][2]*headScene.z;
    }
    float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
    const float eyePosition[3]={head.m[0][3]+anchor.m[0][0]*(eyeIndex?.032f:-.032f),head.m[1][3]+anchor.m[1][0]*(eyeIndex?.032f:-.032f),head.m[2][3]+anchor.m[2][0]*(eyeIndex?.032f:-.032f)};
    const float planeCenter[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]},origin[3]={0,-.2f,0};
    glUseProgram(program);vector("center",planeCenter[0],planeCenter[1],planeCenter[2]);vector("eye",eyePosition[0],eyePosition[1],eyePosition[2]);
    vector("right",anchor.m[0][0],anchor.m[1][0],anchor.m[2][0]);vector("up",anchor.m[0][1],anchor.m[1][1],anchor.m[2][1]);vector("forward",-anchor.m[0][2],-anchor.m[1][2],-anchor.m[2][2]);
    vector("panelRight",r[0],r[1],r[2]);vector("panelUp",u[0],u[1],u[2]);vector("panelForward",f[0],f[1],f[2]);
    glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),1.2f*gridScale);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(0,0,pixels,pixels);glClearColor(0,0,0,0);glDepthMask(GL_TRUE);glDepthFunc(GL_LESS);glClear(sharedDepth==2?GL_COLOR_BUFFER_BIT:GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const int targetKey=tileKey?tileKey:-1;
    if(!tileKey)for(auto &object:scene.objects)object.renderGroup=std::find(ids.begin(),ids.end(),object.id)!=ids.end()?1:0;
    std::vector<ProjectedBounds> projected;ProjectedBounds combined;
    if(cull){
      for(const auto &object:scene.objects){
        projected.push_back(projectedBounds(object,scene.assets[object.asset-1],anchor,eyePosition,r,u,f));
        if(object.visible&&objectSurfaceKey(object)==targetKey){const auto &b=projected.back();combined.valid&=b.valid;combined.x0=std::min(combined.x0,b.x0);combined.x1=std::max(combined.x1,b.x1);combined.y0=std::min(combined.y0,b.y0);combined.y1=std::max(combined.y1,b.y1);combined.near=std::min(combined.near,b.near);combined.far=std::max(combined.far,b.far);}
      }
    }
    ScissorRegion region{};if(cull)region=projectedScissor(combined,planeCenter,eyePosition,r,u,f,1.2f*gridScale,pixels);
    const GLint yawLocation=glGetUniformLocation(program,"yaw"),pitchLocation=glGetUniformLocation(program,"pitch"),rollLocation=glGetUniformLocation(program,"roll");
    const GLint scaleLocation=glGetUniformLocation(program,"modelScale"),offsetLocation=glGetUniformLocation(program,"offset");
    const GLint factorLocation=glGetUniformLocation(program,"factor"),modeLocation=glGetUniformLocation(program,"alphaMode"),cutoffLocation=glGetUniformLocation(program,"alphaCutoff"),sheenLocation=glGetUniformLocation(program,"sheen");
    auto draw=[&](int key,int pass){
      ModelCulling culling(materialCulling);
      auto selected=[&](const Object &object){
        if(!object.visible||(pass<0?objectSurfaceKey(object)==key:objectSurfaceKey(object)!=key))return false;
        if(cull&&pass<0&&!mayOcclude(projected[size_t(&object-scene.objects.data())],combined))return false;
        return true;
      };
      auto drawObject=[&](const Object &object){
        const auto &asset=scene.assets[object.asset-1];
        glUniform1f(yawLocation,object.yaw);glUniform1f(pitchLocation,object.pitch);glUniform1f(rollLocation,object.roll);glUniform1f(scaleLocation,object.scale);
        glUniform3f(offsetLocation,object.x,object.y+.2f,object.z);
        for(size_t primitive=0;primitive<asset.primitives.size();primitive++){
          const auto &part=asset.primitives[primitive];
          if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
          glUniform4fv(factorLocation,1,part.factor);glUniform1i(modeLocation,part.alphaMode);glUniform1f(cutoffLocation,part.alphaCutoff);glUniform3fv(sheenLocation,1,part.sheen);
          culling.material(part,object.animationPose.get());
          glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);
          if(asset.pbr){
            if(pass<0){++pbrDepthDraws;pbrDepthTriangles+=(object.animationPose?part.vertexCount:modelDrawVertexCount(part,object.lodLevel))/3;}
            else{++pbrColorDraws;pbrColorTriangles+=(object.animationPose?part.vertexCount:modelDrawVertexCount(part,object.lodLevel))/3;}
            const bool depthOnly=fastPbrDepth&&pass<0;
            const size_t lightCount=objectLightSets.empty()?0:objectLightSets[size_t(&object-scene.objects.data())].count;
            PbrProgram *variant=nullptr;
            if(specializePbr&&!depthOnly)variant=prewarmedPbr?pbrVariants.ready(asset,part,lightCount,pixels):pbrVariants.get(asset,part,lightCount);
            if(preparedDirectional&&!depthOnly)variant=directionalVariants.ready(asset,part,lightCount,pixels);
            if(referenceLightLoop)assert(depthOnly?referenceLightDepthProgram.initWithLightLoop(directionalLightReferenceLoop,true):referenceLightProgram.initWithLightLoop(directionalLightReferenceLoop));
            auto &activePbr=referenceLightLoop?(depthOnly?referenceLightDepthProgram:referenceLightProgram):(variant?*variant:(depthOnly?depthPbrProgram:pbrProgram));
            assert(activePbr.init(depthOnly));
            activePbr.view(anchor,planeCenter,eyePosition,r,u,f,1.2f*gridScale);
            activePbr.object(object.pitch,object.yaw,object.roll,object.scale,object.x,object.y+.2f,object.z);
            if(pass>=0){
              // Without selection, objectLightSets is never populated.
              const std::span<const ResolvedPointLight> drawLights=selectLocalLights?objectLightSets[size_t(&object-scene.objects.data())].values():std::span<const ResolvedPointLight>(frameLights);
              activePbr.lights(drawLights,0,-.2f,0);
              activePbr.shadows(spotShadows,drawLights,0,-.2f,0);
            }
            activePbr.material(asset,part,object.animationPose?object.animationPose->geometry.primitives[primitive]:part,object.animationPose?0:object.lodLevel,pass<0);
            if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);else drawModelPrimitive(part,0,object.lodLevel);
            activePbr.release();glUseProgram(program);continue;
          }
          if(object.animationPose&&bindSkinPrimitive(asset,part,*object.animationPose,SkinUniforms::of(program))){
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,part.skinIbo);glDrawElements(GL_TRIANGLES,GLsizei(part.skinIndices.size()),GL_UNSIGNED_INT,nullptr);unbindSkinPrimitive(SkinUniforms::of(program));
          }else {
            if(object.animationPose&&drawRigidBatch(asset,*object.animationPose,primitive))continue;
            if(object.animationPose)drawPosePrimitive(part,*object.animationPose,primitive);
            else drawModelPrimitive(part,0,object.lodLevel);
          }
        }
      };
      if(instancing.mode!=ModelInstancing::Off){
        std::vector<const Object*> objects;for(const auto &object:scene.objects)if(selected(object))objects.push_back(&object);
        instancing.draw(scene,culling,objects,pass,program,anchor,planeCenter,eyePosition,r,u,f,1.2f*gridScale,origin,drawObject,[](unsigned){});return;
      }
      for(const auto &object:scene.objects)if(selected(object))drawObject(object);
    };
    if(sharedDepth==1){
      glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);draw(-999,-1);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    }else if(sharedDepth==2){
      glDepthMask(GL_FALSE);glDepthFunc(GL_LEQUAL);draw(targetKey,0);
      glDepthFunc(GL_LESS);glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);draw(targetKey,1);
      glDisable(GL_BLEND);glDepthMask(GL_TRUE);
    }else renderObjectLayers(targetKey,draw,cull?&region:nullptr);
    if(!tileKey)for(auto &object:scene.objects)object.renderGroup=0;
    if(!readback)return {};
    std::vector<unsigned char> image(pixels*pixels*4);glReadPixels(0,0,pixels,pixels,GL_RGBA,GL_UNSIGNED_BYTE,image.data());assert(glGetError()==GL_NO_ERROR);return image;
  }

std::vector<unsigned char> Renderer::render(int id,physics::Vec3 headScene,int eyeIndex,bool occlusion,bool bounds,int &sample,bool scissor,bool crop,bool expanded,bool cached){
    auto &target=scene.objects[size_t(id)-1];
    auto panel=anchor,head=anchor;
    panel.m[0][3]=target.x;panel.m[1][3]=target.y;panel.m[2][3]=-target.z;
    head.m[0][3]=headScene.x;head.m[1][3]=headScene.y;head.m[2][3]=-headScene.z;
    float r[3],u[3],f[3];faceViewer(panel,anchor,head,r,u,f);
    float eye[3]={head.m[0][3]+(eyeIndex?.032f:-.032f),head.m[1][3],head.m[2][3]};
    glUseProgram(program);vector("center",panel.m[0][3],panel.m[1][3],panel.m[2][3]);vector("eye",eye[0],eye[1],eye[2]);
    vector("panelRight",r[0],r[1],r[2]);vector("panelUp",u[0],u[1],u[2]);vector("panelForward",f[0],f[1],f[2]);
    const float referenceWidth=std::max(.55f,target.scale*2.1f);
    const float center[3]={panel.m[0][3],panel.m[1][3],panel.m[2][3]};
    float eyes[2][3]={{head.m[0][3]-.032f,head.m[1][3],head.m[2][3]},{head.m[0][3]+.032f,head.m[1][3],head.m[2][3]}};
    FrameBoundsCache frameBounds;if(cached)frameBounds.update(scene,anchor,head,eyes);
    ProjectedBounds both[2];for(int i=0;i<2;i++)both[i]=cached?frameBounds.project(target,eyes[i],r,u,f):projectedBounds(target,scene.assets[target.asset-1],anchor,eyes[i],r,u,f);
    const int size=crop?objectCropPixels(both,center,eyes,r,u,f,referenceWidth,pixels):pixels;
    const float width=referenceWidth*size/pixels;glUniform1f(glGetUniformLocation(program,"panelHalfWidth"),width*.5f);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glViewport(eyeIndex*pixels,0,size,size);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    const auto targetBounds=cached?frameBounds.project(target,eye,r,u,f):projectedBounds(target,scene.assets[size_t(target.asset)-1],anchor,eye,r,u,f);
    auto draw=[&](int targetId,int pass){
      if(pass<0&&!occlusion)return;
      ModelCulling culling(materialCulling);
      for(const auto &object:scene.objects){
        if(!object.visible||(pass<0?object.id==targetId:object.id!=targetId))continue;
        const auto &asset=scene.assets[size_t(object.asset)-1];
        if(pass<0&&bounds&&!mayOcclude(cached?frameBounds.project(object,eye,r,u,f):projectedBounds(object,asset,anchor,eye,r,u,f),targetBounds))continue;
        glUniform1f(glGetUniformLocation(program,"yaw"),object.yaw);glUniform1f(glGetUniformLocation(program,"pitch"),object.pitch);glUniform1f(glGetUniformLocation(program,"roll"),object.roll);
        glUniform1f(glGetUniformLocation(program,"modelScale"),object.scale);vector("offset",object.x-target.x,object.y-target.y,object.z-target.z);
        for(const auto &part:asset.primitives){
          if(pass<0?part.alphaMode==2:(part.alphaMode==2)!=bool(pass))continue;
          glUniform4fv(glGetUniformLocation(program,"factor"),1,part.factor);glUniform1i(glGetUniformLocation(program,"alphaMode"),part.alphaMode);glUniform1f(glGetUniformLocation(program,"alphaCutoff"),part.alphaCutoff);
          glUniform3fv(glGetUniformLocation(program,"sheen"),1,part.sheen);
          culling.material(part,object.animationPose.get());
          GLuint reference=0;
          if(expanded){auto &buffer=expandedBuffers[&part];if(!buffer){glGenBuffers(1,&buffer);glBindBuffer(GL_ARRAY_BUFFER,buffer);glBufferData(GL_ARRAY_BUFFER,part.vertices.size()*sizeof(float),part.vertices.data(),GL_STATIC_DRAW);}reference=buffer;}
          glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,part.texture);drawModelPrimitive(part,reference);
        }
      }
    };
    auto region=projectedScissor(targetBounds,center,eye,r,u,f,width*.5f,size);
    region.x+=eyeIndex*pixels;
    renderObjectLayers(id,draw,scissor?&region:nullptr);
    // Pixel along the ray through the red cube's center in this overlay.
    const auto &box=scene.objects[2];float ray[3]={box.x-eye[0],box.y-eye[1],-box.z-eye[2]};
    float distance=0,plane=0;for(int axis=0;axis<3;axis++){distance+=ray[axis]*f[axis];plane+=(panel.m[axis][3]-eye[axis])*f[axis];}
    float x=0,y=0;for(int axis=0;axis<3;axis++){const float delta=eye[axis]+ray[axis]*plane/distance-panel.m[axis][3];x+=delta*r[axis];y+=delta*u[axis];}
    const int px=int((x/referenceWidth+.5f)*pixels),py=int((y/referenceWidth+.5f)*pixels);assert(px>=0&&px<pixels&&py>=0&&py<pixels);sample=(py*pixels+px)*4;
    std::vector<unsigned char> result(pixels*pixels*4),cropped(size*size*4);
    glReadPixels(eyeIndex*pixels,0,size,size,GL_RGBA,GL_UNSIGNED_BYTE,cropped.data());
    const int border=(pixels-size)/2;for(int y=0;y<size;y++)std::copy_n(cropped.data()+y*size*4,size*4,result.data()+((y+border)*pixels+border)*4);
    assert(glGetError()==GL_NO_ERROR);return result;
  }
