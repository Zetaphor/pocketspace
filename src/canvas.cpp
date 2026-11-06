#include "canvas.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <map>

namespace {
FT_Library library=nullptr;
// Faces stay open for the process; text runs on one thread.
std::map<std::string,FT_Face> faces;

FT_Face face(const std::string &path){
  if(path.empty()||!library)return nullptr;
  if(const auto found=faces.find(path);found!=faces.end())return found->second;
  FT_Face loaded=nullptr;
  if(FT_New_Face(library,path.c_str(),0,&loaded)!=0)loaded=nullptr;
  faces.emplace(path,loaded);
  return loaded;
}

void pixel(Canvas &p,int x,int y,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  if(x<0||y<0||x>=p.width||y>=p.height)return;
  auto *q=&p.rgba[size_t(y*p.width+x)*4];
  const float alpha=a/255.f,old=1-alpha;
  q[0]=(unsigned char)(r*alpha+q[0]*old);
  q[1]=(unsigned char)(g*alpha+q[1]*old);
  q[2]=(unsigned char)(b*alpha+q[2]*old);
  q[3]=(unsigned char)std::min(255.f,a+q[3]*old);
}
// Blends with coverage in [0, 1] scaling the color's alpha.
void cover(Canvas &p,int x,int y,unsigned char r,unsigned char g,unsigned char b,unsigned char a,float coverage){
  if(coverage<=0)return;
  pixel(p,x,y,r,g,b,(unsigned char)std::lround(a*std::min(1.f,coverage)));
}

// The next code point of UTF-8 text; invalid bytes read as U+FFFD.
uint32_t next(const std::string &text,size_t &i){
  const auto c=(unsigned char)text[i++];
  if(c<0x80)return c;
  int extra=c>=0xf0?3:c>=0xe0?2:c>=0xc0?1:-1;
  if(extra<0)return 0xfffd;
  uint32_t code=c&(0x3f>>extra);
  for(;extra>0;extra--){
    if(i>=text.size()||((unsigned char)text[i]&0xc0)!=0x80)return 0xfffd;
    code=(code<<6)|((unsigned char)text[i++]&0x3f);
  }
  return code;
}

// Walks one or more lines of text, calling draw for each loaded glyph at its pen position.
template<class Draw> int layout(FT_Face f,const std::string &text,int pixels,float tracking,Draw &&draw){
  FT_Set_Pixel_Sizes(f,0,pixels);
  float pen=0,widest=0;int line=0;FT_UInt previous=0;
  for(size_t i=0;i<text.size();){
    const uint32_t code=next(text,i);
    if(code=='\n'){widest=std::max(widest,pen);pen=0;line++;previous=0;continue;}
    const FT_UInt glyph=FT_Get_Char_Index(f,code);
    if(previous&&glyph&&FT_HAS_KERNING(f)){FT_Vector kern{};if(FT_Get_Kerning(f,previous,glyph,FT_KERNING_DEFAULT,&kern)==0)pen+=kern.x/64.f;}
    if(FT_Load_Glyph(f,glyph,FT_LOAD_DEFAULT)!=0)continue;
    draw(f->glyph,pen,line);
    pen+=f->glyph->advance.x/64.f+tracking;
    previous=glyph;
  }
  return int(std::lround(std::max(widest,pen)-(text.empty()?0:tracking)));
}
}

std::string canvasDefaultFont(){
  const char *env=std::getenv("SF_FONT");
  if(env&&*env)return env;
  for(const char *p:{"/usr/share/fonts/TTF/Hack-Regular.ttf","/usr/share/fonts/noto/NotoSansMono-Regular.ttf","/usr/share/fonts/gnu-free/FreeMono.otf",
                     "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf","/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf","/usr/share/fonts/truetype/freefont/FreeMono.ttf"})
    if(std::filesystem::exists(p))return p;
  return {};
}
bool canvasInitLibrary(){return library||FT_Init_FreeType(&library)==0;}
void canvasInit(Canvas &p,int w,int h,const std::string &font){
  p.width=w;p.height=h;p.fontPath=font.empty()?canvasDefaultFont():font;
  p.rgba.assign(size_t(w)*h*4,0);p.dirty=true;
}
void canvasClear(Canvas &p,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  for(size_t i=0;i<p.rgba.size();i+=4){p.rgba[i]=r;p.rgba[i+1]=g;p.rgba[i+2]=b;p.rgba[i+3]=a;}
  p.dirty=true;
}
void canvasRect(Canvas &p,int x,int y,int w,int h,unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool fill){
  for(int yy=std::max(0,y);yy<std::min(p.height,y+h);yy++)
    for(int xx=std::max(0,x);xx<std::min(p.width,x+w);xx++)
      if(fill||xx==x||yy==y||xx==x+w-1||yy==y+h-1)pixel(p,xx,yy,r,g,b,a);
  p.dirty=true;
}
void canvasLine(Canvas &p,int x1,int y1,int x2,int y2,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  int dx=std::abs(x2-x1),sx=x1<x2?1:-1,dy=-std::abs(y2-y1),sy=y1<y2?1:-1,err=dx+dy;
  for(;;){
    pixel(p,x1,y1,r,g,b,a);
    if(x1==x2&&y1==y2)break;
    const int e=2*err;
    if(e>=dy){err+=dy;x1+=sx;}
    if(e<=dx){err+=dx;y1+=sy;}
  }
  p.dirty=true;
}
bool canvasText(Canvas &p,int x,int y,const std::string &text,unsigned char r,unsigned char g,unsigned char b,unsigned char a,int pixels){
  return canvasTextFont(p,p.fontPath,x,y,text,r,g,b,a,pixels,0);
}
bool canvasTextFont(Canvas &p,const std::string &font,int x,int y,const std::string &text,unsigned char r,unsigned char g,unsigned char b,unsigned char a,int pixels,float tracking){
  FT_Face f=face(font);
  if(!f)return false;
  FT_Set_Pixel_Sizes(f,0,pixels);
  const int ascender=int(f->size->metrics.ascender>>6);
  layout(f,text,pixels,tracking,[&](FT_GlyphSlot glyph,float pen,int line){
    if(FT_Render_Glyph(glyph,FT_RENDER_MODE_NORMAL)!=0)return;
    const auto &bm=glyph->bitmap;
    const int left=x+int(std::lround(pen))+glyph->bitmap_left,top=y+ascender+line*(pixels+4)-glyph->bitmap_top;
    for(unsigned row=0;row<bm.rows;row++)for(unsigned col=0;col<bm.width;col++){
      const unsigned char v=bm.buffer[row*bm.pitch+col];
      if(v)pixel(p,left+int(col),top+int(row),r,g,b,(unsigned char)(v*a/255));
    }
  });
  p.dirty=true;
  return true;
}
int canvasTextWidth(const Canvas &p,const std::string &text,int pixels){return canvasTextWidthFont(p.fontPath,text,pixels,0);}
int canvasTextWidthFont(const std::string &font,const std::string &text,int pixels,float tracking){
  FT_Face f=face(font);
  if(!f)return -1;
  return layout(f,text,pixels,tracking,[](FT_GlyphSlot,float,int){});
}
void canvasCircle(Canvas &p,int cx,int cy,int radius,unsigned char r,unsigned char g,unsigned char b,unsigned char a,bool fill){
  for(int y=-radius;y<=radius;y++)for(int x=-radius;x<=radius;x++){
    const int d=x*x+y*y;
    if((fill&&d<=radius*radius)||(!fill&&d<=radius*radius&&d>=(radius-1)*(radius-1)))pixel(p,cx+x,cy+y,r,g,b,a);
  }
  p.dirty=true;
}
void canvasFillRoundRect(Canvas &p,float x,float y,float w,float h,float radius,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  radius=std::clamp(radius,0.f,std::min(w,h)/2);
  const float cx=x+w/2,cy=y+h/2,hx=w/2-radius,hy=h/2-radius;
  for(int yy=std::max(0,int(std::floor(y)));yy<std::min(p.height,int(std::ceil(y+h)));yy++)
    for(int xx=std::max(0,int(std::floor(x)));xx<std::min(p.width,int(std::ceil(x+w)));xx++){
      // Signed distance from the pixel center to the rounded rectangle's edge.
      const float qx=std::max(std::abs(xx+.5f-cx)-hx,0.f),qy=std::max(std::abs(yy+.5f-cy)-hy,0.f);
      const float inside=std::min(std::max(std::abs(xx+.5f-cx)-hx,std::abs(yy+.5f-cy)-hy),0.f);
      const float distance=std::sqrt(qx*qx+qy*qy)+inside-radius;
      cover(p,xx,yy,r,g,b,a,.5f-distance);
    }
  p.dirty=true;
}
void canvasFillCircleSmooth(Canvas &p,float cx,float cy,float radius,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  for(int yy=std::max(0,int(cy-radius-1));yy<std::min(p.height,int(cy+radius+2));yy++)
    for(int xx=std::max(0,int(cx-radius-1));xx<std::min(p.width,int(cx+radius+2));xx++)
      cover(p,xx,yy,r,g,b,a,radius+.5f-std::hypot(xx+.5f-cx,yy+.5f-cy));
  p.dirty=true;
}
void canvasRing(Canvas &p,float cx,float cy,float radius,float thickness,unsigned char r,unsigned char g,unsigned char b,unsigned char a){
  const float inner=radius-thickness;
  for(int yy=std::max(0,int(cy-radius-1));yy<std::min(p.height,int(cy+radius+2));yy++)
    for(int xx=std::max(0,int(cx-radius-1));xx<std::min(p.width,int(cx+radius+2));xx++){
      const float d=std::hypot(xx+.5f-cx,yy+.5f-cy);
      cover(p,xx,yy,r,g,b,a,std::min(radius+.5f-d,d-inner+.5f));
    }
  p.dirty=true;
}
void canvasBlit(Canvas &p,int x,int y,int w,int h,const unsigned char *rgba){
  for(int yy=0;yy<h;yy++)for(int xx=0;xx<w;xx++){auto *src=&rgba[size_t(yy*w+xx)*4];pixel(p,x+xx,y+yy,src[0],src[1],src[2],src[3]);}
  p.dirty=true;
}
