#pragma once
// Test-only combined indexed streams. Preserve primitive and instance order;
// texture objects retain their original dimensions, filtering and wrap modes.
struct MultipartInstancingExperiment {
  static constexpr size_t materialLimit=8;
  struct Mesh {GLuint vertices=0,indices=0;GLsizei count=0;uint64_t bytes=0;bool attempted=false;};
  std::map<int,Mesh> meshes;
  bool enabled=false;
  static bool eligible(const ModelAsset &asset){
    return asset.primitives.size()>1&&asset.primitives.size()<=materialLimit&&
      std::all_of(asset.primitives.begin(),asset.primitives.end(),[](const auto &p){return p.alphaMode!=2&&p.vbo&&p.vertexCount>0;});
  }
  static bool read(GLenum target,GLuint buffer,size_t bytes,void *out){
    glBindBuffer(target,buffer);void *mapped=glMapBufferRange(target,0,bytes,GL_MAP_READ_BIT);
    if(!mapped)return false;std::memcpy(out,mapped,bytes);return glUnmapBuffer(target)==GL_TRUE;
  }
  Mesh *get(int key,const ModelAsset &asset){
    auto &mesh=meshes[key];if(mesh.attempted)return mesh.vertices&&mesh.indices?&mesh:nullptr;mesh.attempted=true;
    std::vector<float> vertices;std::vector<uint32_t> indices;
    for(size_t slot=0;slot<asset.primitives.size();slot++){
      const auto &p=asset.primitives[slot];const uint32_t base=vertices.size()/6;
      std::vector<float> original(size_t(p.gpuVertexCount)*5);
      if(!read(GL_ARRAY_BUFFER,p.vbo,original.size()*sizeof(float),original.data()))return nullptr;
      for(size_t n=0;n<original.size();n+=5){vertices.insert(vertices.end(),original.begin()+n,original.begin()+n+5);vertices.push_back(float(slot));}
      if(p.indexBuffer){
        if(p.indexType==GL_UNSIGNED_SHORT){std::vector<uint16_t> data(p.vertexCount);if(!read(GL_ELEMENT_ARRAY_BUFFER,p.indexBuffer,data.size()*sizeof(uint16_t),data.data()))return nullptr;for(auto index:data)indices.push_back(base+index);}
        else{std::vector<uint32_t> data(p.vertexCount);if(!read(GL_ELEMENT_ARRAY_BUFFER,p.indexBuffer,data.size()*sizeof(uint32_t),data.data()))return nullptr;for(auto index:data)indices.push_back(base+index);}
      }else for(int n=0;n<p.vertexCount;n++)indices.push_back(base+n);
    }
    glGenBuffers(1,&mesh.vertices);glBindBuffer(GL_ARRAY_BUFFER,mesh.vertices);glBufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(float),vertices.data(),GL_STATIC_DRAW);
    glGenBuffers(1,&mesh.indices);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,mesh.indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,indices.size()*sizeof(uint32_t),indices.data(),GL_STATIC_DRAW);
    GLint v=0,i=0;glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER,GL_BUFFER_SIZE,&i);glBindBuffer(GL_ARRAY_BUFFER,mesh.vertices);glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&v);
    if(size_t(v)!=vertices.size()*sizeof(float)||size_t(i)!=indices.size()*sizeof(uint32_t)){glDeleteBuffers(1,&mesh.vertices);glDeleteBuffers(1,&mesh.indices);mesh.vertices=mesh.indices=0;return nullptr;}
    mesh.bytes=uint64_t(v)+uint64_t(i);mesh.count=indices.size();return &mesh;
  }
  static std::string fragment(){
    std::string source="#version 300 es\nprecision mediump float;in vec2 t;flat in highp int materialSlot;uniform vec4 factors[8];uniform vec3 sheens[8];uniform int modes[8];uniform float cutoffs[8];out vec4 color;\n";
    for(int n=0;n<8;n++)source+="uniform sampler2D texture"+std::to_string(n)+";\n";
    source+="void main(){vec2 dx=dFdx(t),dy=dFdy(t);vec4 texel;";
    for(int n=0;n<8;n++)source+=(n?"else ":"")+std::string("if(materialSlot==")+std::to_string(n)+")texel=textureGrad(texture"+std::to_string(n)+",t,dx,dy);";
    source+="else texel=vec4(0);vec4 factor=factors[materialSlot];vec3 sheen=sheens[materialSlot];int alphaMode=modes[materialSlot];float alphaCutoff=cutoffs[materialSlot];color=texel*factor;color.rgb=clamp(color.rgb+texel.rgb*sheen*0.8,0.0,1.0);if(alphaMode==1&&color.a<alphaCutoff)discard;if(alphaMode==0)color.a=1.0;if(alphaMode==1)color.rgb*=color.a;}";return source;
  }
  void reset(){for(auto &[key,m]:meshes){if(m.vertices)glDeleteBuffers(1,&m.vertices);if(m.indices)glDeleteBuffers(1,&m.indices);}meshes.clear();}
  uint64_t bytes()const{uint64_t total=0;for(const auto &[key,m]:meshes)total+=m.bytes;return total;}
  ~MultipartInstancingExperiment(){reset();}
};
