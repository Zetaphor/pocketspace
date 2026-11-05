#include "panel.h"
#include "../stb_image.h"
bool panelInit(Panel&p,int id,int w,int h,float wm,float hm,const std::string&font){p={};p.id=id;p.widthMeters=wm;p.heightMeters=hm;canvasInit(p,w,h,font);return canvasInitLibrary();}
bool panelImage(Panel&p,int x,int y,const std::string&path){int w=0,h=0,n=0;unsigned char*data=stbi_load(path.c_str(),&w,&h,&n,4);if(!data)return false;canvasBlit(p,x,y,w,h,data);stbi_image_free(data);return true;}
void panelUpload(Panel&p){if(!p.texture)glGenTextures(1,&p.texture);glBindTexture(GL_TEXTURE_2D,p.texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,p.width,p.height,0,GL_RGBA,GL_UNSIGNED_BYTE,p.rgba.data());glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);++p.textureRevision;p.dirty=false;}
void panelDestroy(Panel&p){if(p.texture)glDeleteTextures(1,&p.texture);p.texture=0;p.rgba.clear();}
