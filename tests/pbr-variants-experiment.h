#pragma once
// Bounded test-only program ownership; overflow/compile failure uses general PBR.
struct PbrVariantPrograms {
 static constexpr size_t limit=16;
 std::map<int,std::unique_ptr<PbrProgram>> programs;
 std::set<int> failed;bool rejectNewPrograms=false;
 size_t fallbackDraws=0;double compileMs=0;
 PbrProgram *get(const ModelAsset &asset,const PrimitiveAsset &part,size_t count){
  int key=0;for(int i=0;i<4;i++)if(part.maps[i].texture)key|=1<<i;
  if(part.materialUnlit)key|=16;if(part.doubleSided)key|=32;
  if(!part.materialUnlit&&asset.environment&&asset.lighting.environmentIntensity>0&&asset.environment->upload())key|=64;
  key|=part.alphaMode<<7;key|=int(count)<<9;
  if(auto found=programs.find(key);found!=programs.end())return found->second.get();
  if(failed.count(key)||programs.size()+failed.size()==limit){fallbackDraws++;return nullptr;}
  auto program=std::make_unique<PbrProgram>();auto start=std::chrono::steady_clock::now();bool ok=!rejectNewPrograms&&program->init(false,key);
  compileMs+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
  if(!ok){failed.insert(key);fallbackDraws++;return nullptr;}
  auto *result=program.get();programs.emplace(key,std::move(program));return result;
 }
 uint64_t binaryBytes()const{uint64_t total=0;for(const auto &[key,p]:programs){GLint bytes=0;glGetProgramiv(p->experimentId(),GL_PROGRAM_BINARY_LENGTH,&bytes);if(bytes>0)total+=uint64_t(bytes);}return total;}
 void clear(){programs.clear();failed.clear();}
};
