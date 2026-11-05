#pragma once
#include "canvas.h"
#include <GLES3/gl3.h>
#include <cstdint>
// A scene panel: a canvas plus its placement and GL texture.
struct Panel : Canvas {int id=0;float widthMeters=1,heightMeters=1,x=0,y=0,z=0;float rotation[9]={1,0,0,0,1,0,0,0,1};std::string mode="world";bool visible=true,interactive=false;GLuint texture=0;uint64_t textureRevision=0;int container=0;};
bool panelInit(Panel &,int,int,int,float,float,const std::string &);bool panelImage(Panel &,int,int,const std::string &);void panelUpload(Panel &);void panelDestroy(Panel &);
