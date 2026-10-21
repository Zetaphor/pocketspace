#pragma once
#include <GLES3/gl3.h>
#include <string>
#include <vector>
struct Panel {int id=0,width=0,height=0;float widthMeters=1,heightMeters=1,x=0,y=0,z=-1;float rotation[9]={1,0,0,0,1,0,0,0,1};std::string mode="world";bool visible=true,interactive=false,dirty=true;GLuint texture=0;std::vector<unsigned char> rgba;std::string fontPath;int fontPixels=32;};
bool panelInit(Panel &,int,int,int,float,float,const std::string &);void panelClear(Panel &,unsigned char,unsigned char,unsigned char,unsigned char);void panelRect(Panel &,int,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char,bool);void panelLine(Panel &,int,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char);void panelCircle(Panel &,int,int,int,unsigned char,unsigned char,unsigned char,unsigned char,bool);bool panelText(Panel &,int,int,const std::string &,unsigned char,unsigned char,unsigned char,unsigned char,int);bool panelImage(Panel &,int,int,const std::string &);void panelUpload(Panel &);void panelDestroy(Panel &);
