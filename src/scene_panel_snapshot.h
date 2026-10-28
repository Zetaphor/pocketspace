#pragma once
// Included inside scenePersistence; pixel snapshots have no external codec dependency.
constexpr size_t maxPanelBytes=64*1024*1024;
constexpr uint32_t maxPanels=32;
static std::string base64(const std::vector<unsigned char> &data){
  static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  require((data.size()+2)/3*4<=maxBytes,"panel content exceeds snapshot file limit");std::string out;out.reserve((data.size()+2)/3*4);
  for(size_t i=0;i<data.size();i+=3){const uint32_t n=uint32_t(data[i])<<16|(i+1<data.size()?uint32_t(data[i+1])<<8:0)|(i+2<data.size()?data[i+2]:0);out+=alphabet[n>>18];out+=alphabet[(n>>12)&63];out+=i+1<data.size()?alphabet[(n>>6)&63]:'=';out+=i+2<data.size()?alphabet[n&63]:'=';}return out;
}
static std::vector<unsigned char> unbase64(const std::string &data){
  require(data.size()%4==0&&data.size()<=maxBytes,"invalid panel base64 length");std::vector<unsigned char> out;out.reserve(data.size()/4*3);
  auto digit=[](char c)->int{if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;};
  for(size_t i=0;i<data.size();i+=4){const int a=digit(data[i]),b=digit(data[i+1]),c=data[i+2]=='='?0:digit(data[i+2]),d=data[i+3]=='='?0:digit(data[i+3]);const bool pad2=data[i+2]=='=',pad1=data[i+3]=='=';
    require(a>=0&&b>=0&&c>=0&&d>=0&&(!pad2||pad1)&&(!(pad1||pad2)||i+4==data.size())&&(!pad2||(b&15)==0)&&(!pad1||pad2||(c&3)==0),"invalid panel base64");
    const uint32_t n=uint32_t(a)<<18|uint32_t(b)<<12|uint32_t(c)<<6|uint32_t(d);out.push_back(n>>16);if(!pad2)out.push_back(n>>8);if(!pad1)out.push_back(n);
  }return out;
}
static JSValue pixelsValue(JSContext *ctx,const Panel &p){
  std::vector<unsigned char> runs;
  for(size_t i=0;i<p.rgba.size();){size_t end=i+4;while(end<p.rgba.size()&&std::equal(p.rgba.begin()+i,p.rgba.begin()+i+4,p.rgba.begin()+end))end+=4;
    const uint32_t count=uint32_t((end-i)/4);for(int b=0;b<4;b++)runs.push_back((count>>(b*8))&255);runs.insert(runs.end(),p.rgba.begin()+i,p.rgba.begin()+i+4);i=end;if(runs.size()>=p.rgba.size())break;
  }
  const bool compressed=runs.size()<p.rgba.size();Value value(ctx,JS_NewObject(ctx));put(ctx,value,"encoding",JS_NewString(ctx,compressed?"rgba8-rle-base64":"rgba8-base64"));
  const auto data=base64(compressed?runs:p.rgba);put(ctx,value,"data",JS_NewStringLen(ctx,data.data(),data.size()));return JS_DupValue(ctx,value);
}
static std::vector<unsigned char> pixelsRead(JSContext *ctx,JSValueConst value,size_t expected){
  auto encoding=textField(ctx,value,"encoding");require(encoding=="rgba8-base64"||encoding=="rgba8-rle-base64","unknown panel pixel encoding");auto encoded=field(ctx,value,"data");auto bytes=unbase64(string(ctx,encoded));
  if(encoding=="rgba8-base64"){require(bytes.size()==expected,"panel pixel length mismatch");return bytes;}
  require(bytes.size()%8==0,"invalid panel pixel runs");std::vector<unsigned char> pixels;pixels.reserve(expected);
  for(size_t i=0;i<bytes.size();i+=8){uint32_t count=0;for(int b=0;b<4;b++)count|=uint32_t(bytes[i+b])<<(b*8);require(count>0&&count<=(expected-pixels.size())/4,"panel pixel run exceeds dimensions");for(uint32_t n=0;n<count;n++)pixels.insert(pixels.end(),bytes.begin()+i+4,bytes.begin()+i+8);}
  require(pixels.size()==expected,"panel pixel length mismatch");return pixels;
}
static bool panelMode(const std::string &mode){return mode=="world"||mode=="viewerFacing"||mode=="headLocked"||mode=="arbitrary";}
static JSValue panelValue(JSContext *ctx,const Panel &p,size_t &total){
  require(p.width>=1&&p.width<=4096&&p.height>=1&&p.height<=4096,"invalid saved panel dimensions");const size_t bytes=size_t(p.width)*p.height*4;
  require(bytes<=maxPanelBytes-total&&p.rgba.size()==bytes,"saved panel pixels exceed 64 MiB or mismatch dimensions");total+=bytes;
  require(std::isfinite(p.widthMeters)&&p.widthMeters>0&&p.widthMeters<=10&&panelMode(p.mode)&&p.fontPixels>=1&&p.fontPixels<=4096&&std::isfinite(p.heightMeters)&&p.heightMeters>0&&std::abs(double(p.heightMeters)-double(p.widthMeters)*p.height/p.width)<=1e-5*std::max(1.,double(p.heightMeters)),"invalid saved panel placement/font settings");
  Value value(ctx,JS_NewObject(ctx)),rotation(ctx,JS_NewArray(ctx));put(ctx,value,"id",JS_NewInt32(ctx,p.id));put(ctx,value,"widthPixels",JS_NewInt32(ctx,p.width));put(ctx,value,"heightPixels",JS_NewInt32(ctx,p.height));put(ctx,value,"widthMeters",JS_NewFloat64(ctx,p.widthMeters));put(ctx,value,"heightMeters",JS_NewFloat64(ctx,p.heightMeters));
  put(ctx,value,"position",vectorValue(ctx,{p.x,p.y,p.z}));put(ctx,value,"mode",JS_NewString(ctx,p.mode.c_str()));
  for(int i=0;i<9;i++){require(std::isfinite(p.rotation[i]),"non-finite panel rotation");putIndex(ctx,rotation,i,JS_NewFloat64(ctx,p.rotation[i]));}put(ctx,value,"rotation",JS_DupValue(ctx,rotation));
  put(ctx,value,"visible",JS_NewBool(ctx,p.visible));put(ctx,value,"interactive",JS_NewBool(ctx,p.interactive));put(ctx,value,"fontPath",JS_NewString(ctx,p.fontPath.c_str()));put(ctx,value,"fontPixels",JS_NewInt32(ctx,p.fontPixels));put(ctx,value,"pixels",pixelsValue(ctx,p));return JS_DupValue(ctx,value);
}
static Panel panelRead(JSContext *ctx,JSValueConst value,int id,int owner,size_t &total){
  const int w=int(number(ctx,value,"widthPixels",1,4096,true)),h=int(number(ctx,value,"heightPixels",1,4096,true));const double width=number(ctx,value,"widthMeters",0,10);require(width>0&&float(width)>0,"panel widthMeters must be positive");const double height=number(ctx,value,"heightMeters",0,40960);require(height>0&&float(height)>0&&std::abs(height-width*h/w)<=1e-5*std::max(1.,height),"panel physical aspect ratio mismatch");
  const size_t bytes=size_t(w)*h*4;require(bytes<=maxPanelBytes-total,"saved panel pixels exceed 64 MiB");total+=bytes;
  const auto mode=textField(ctx,value,"mode");require(panelMode(mode),"invalid panel mode");auto position=field(ctx,value,"position"),rotation=field(ctx,value,"rotation"),pixels=field(ctx,value,"pixels");auto pos=vectorRead(ctx,position);require(length(ctx,rotation,9)==9,"panel rotation requires 9 numbers");
  Panel p;p.id=id;p.width=w;p.height=h;p.widthMeters=float(width);p.heightMeters=float(height);p.container=owner;p.mode=mode;p.x=pos.x;p.y=pos.y;p.z=pos.z;
  for(uint32_t i=0;i<9;i++){Value n(ctx,JS_GetPropertyUint32(ctx,rotation,i));double d=0;require(JS_IsNumber(n)&&JS_ToFloat64(ctx,&d,n)==0&&std::isfinite(d)&&std::abs(d)<=floatLimit,"invalid panel rotation");p.rotation[i]=float(d);}
  p.visible=boolean(ctx,value,"visible");p.interactive=boolean(ctx,value,"interactive");p.fontPath=textField(ctx,value,"fontPath");p.fontPixels=int(number(ctx,value,"fontPixels",1,4096,true));p.rgba=pixelsRead(ctx,pixels,bytes);require(panelInitLibrary(),"panel font library initialization failed");return p;
}
