#pragma once
#include "canvas.h"
#include <GLES3/gl3.h>
#include <cstdint>
// A scene panel: a canvas plus its placement and GL texture.
struct Panel : Canvas {int id=0;float widthMeters=1,heightMeters=1,x=0,y=0,z=0;float rotation[9]={1,0,0,0,1,0,0,0,1};std::string mode="world";bool visible=true,interactive=false;
  // Object placement: the followed object (0 once it is removed) and whether the panel turns to the viewer.
  int objectId=0;bool facesViewer=false;
 GLuint texture=0;uint64_t textureRevision=0;int container=0;};
inline bool panelPlacementMode(const std::string &mode){return mode=="world"||mode=="viewerFacing"||mode=="headLocked"||mode=="arbitrary"||mode=="object";}
bool panelInit(Panel &,int,int,int,float,float,const std::string &);bool panelImage(Panel &,int,int,const std::string &);void panelUpload(Panel &);void panelDestroy(Panel &);
