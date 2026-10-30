#pragma once
// Prepare a bounded program set at scene load; rendering only looks up ready keys.
struct PbrVariantPrograms {
 static constexpr size_t limit=16;
 std::map<int,std::unique_ptr<PbrProgram>> programs;
 std::set<int> failed;
#ifdef SF_PBR_VARIANT_EXPERIMENT
 bool rejectNewPrograms=false;
#endif
 size_t preparations=0,readyDraws=0;
 size_t fallbackDraws=0;double compileMs=0;
 static int keyFor(const ModelAsset &asset,const PrimitiveAsset &part,size_t count){
  int key=0;for(int i=0;i<4;i++)if(part.maps[i].texture)key|=1<<i;
  if(part.materialUnlit)key|=16;if(part.doubleSided)key|=32;
  if(!part.materialUnlit&&asset.environment&&asset.lighting.environmentIntensity>0&&asset.environment->upload())key|=64;
  return key|(part.alphaMode<<7)|(int(count)<<9);
 }
 PbrProgram *ready(const ModelAsset &asset,const PrimitiveAsset &part,size_t count,int pixels){
  if(pixels<1024||count<4||part.materialUnlit)return nullptr;
  auto found=programs.find(keyFor(asset,part,count));if(found!=programs.end()){readyDraws++;return found->second.get();}
  fallbackDraws++;return nullptr;
 }
 template<class SceneType>void prepare(SceneType &scene)try{
  preparations++;clear();
  auto report=[&]{printf("PBR_VARIANTS_PREPARE generation=%zu programs=%zu compile_ms=%.4f\n",preparations,programs.size(),compileMs);fflush(stdout);};
  if(resolvePointLights(scene).size()<4){report();return;}
  // Scene preparation only: draw-time lookups never call get or compile.
  for(const auto &object:scene.objects){const auto &asset=scene.assets[size_t(object.asset)-1];if(!asset.pbr)continue;
   for(const auto &part:asset.primitives)if(!part.materialUnlit)for(int count=4;count<=8;count++)compile(asset,part,count);
  }
  report();
 }catch(const std::exception &error){clear();fprintf(stderr,"[pbr] optional variant preparation failed: %s; using general program\n",error.what());}
#ifdef SF_PBR_VARIANT_EXPERIMENT
 PbrProgram *get(const ModelAsset &asset,const PrimitiveAsset &part,size_t count){return compile(asset,part,count);}
#endif
private:
 PbrProgram *compile(const ModelAsset &asset,const PrimitiveAsset &part,size_t count){
  const int key=keyFor(asset,part,count);
  if(auto found=programs.find(key);found!=programs.end())return found->second.get();
  if(failed.count(key)||programs.size()+failed.size()==limit){fallbackDraws++;return nullptr;}
  auto program=std::make_unique<PbrProgram>();auto start=std::chrono::steady_clock::now();bool ok=true;
#ifdef SF_PBR_VARIANT_EXPERIMENT
  ok=!rejectNewPrograms;
#endif
  if(ok)ok=program->initVariant(key);
  compileMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(!ok){failed.insert(key);fallbackDraws++;return nullptr;}
  auto *result=program.get();programs.emplace(key,std::move(program));return result;
 }
public:
 uint64_t binaryBytes()const{uint64_t total=0;for(const auto &[key,p]:programs){GLint bytes=0;glGetProgramiv(p->id(),GL_PROGRAM_BINARY_LENGTH,&bytes);if(bytes>0)total+=uint64_t(bytes);}return total;}
 void clear(){programs.clear();failed.clear();}
};
