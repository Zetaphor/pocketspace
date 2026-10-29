#pragma once
inline constexpr const char *modelVertexShader=R"(#version 300 es
layout(location=0) in vec3 p;layout(location=1) in vec2 uv;
uniform vec3 center,right,up,forward,panelRight,panelUp,panelForward,eye;
uniform vec3 offset;
uniform float yaw,pitch,roll,modelScale,panelHalfWidth;
out vec2 t;

layout(location=6) in vec4 skinJoints;
layout(location=7) in vec4 skinWeights;
uniform bool skinEnabled;
uniform highp sampler2D skinPalette;
uniform vec4 skinNormalize;
out vec3 gpuSkinPosition;
vec3 skinPoint(){
 vec3 result=vec3(0);
 for(int lane=0;lane<4;lane++){
  int joint=int(skinJoints[lane]);
  mat4 matrix=mat4(texelFetch(skinPalette,ivec2(0,joint),0),texelFetch(skinPalette,ivec2(1,joint),0),texelFetch(skinPalette,ivec2(2,joint),0),texelFetch(skinPalette,ivec2(3,joint),0));
  result+=skinWeights[lane]*(matrix*vec4(p,1)).xyz;
 }
 return (result-skinNormalize.xyz)*skinNormalize.w;
}
void main(){
  vec3 skinVertex=skinEnabled?skinPoint():p;
  gpuSkinPosition=skinVertex;
  float cr=cos(roll),sr=sin(roll),cp=cos(pitch),sp=sin(pitch),cy=cos(yaw),sy=sin(yaw);
  vec3 rolled=vec3(cr*skinVertex.x-sr*skinVertex.y,sr*skinVertex.x+cr*skinVertex.y,skinVertex.z);
  vec3 pitched=vec3(rolled.x,cp*rolled.y-sp*rolled.z,sp*rolled.y+cp*rolled.z);
  vec3 rotated=vec3(cy*pitched.x+sy*pitched.z,pitched.y,-sy*pitched.x+cy*pitched.z);
  vec3 world=center+right*offset.x+up*offset.y+forward*offset.z+modelScale*(right*rotated.x+up*rotated.y-forward*rotated.z);
  vec3 ray=world-eye;
  float distanceAlong=dot(ray,panelForward);
  float hitScale=dot(center-eye,panelForward)/distanceAlong;
  vec3 hit=eye+hitScale*ray;
  float x=dot(hit-center,panelRight)/panelHalfWidth;
  float y=dot(hit-center,panelUp)/panelHalfWidth;
  // Perspective depth must be affine in clip space, not linear after divide.
  // This keeps oblique floors/walls comparable to other meshes at each pixel.
  float clipZ=(5.05*distanceAlong-0.5)/4.95;
  gl_Position=vec4(x*distanceAlong,y*distanceAlong,clipZ,distanceAlong);
  t=uv;
})";
inline constexpr const char *modelFragmentShader=R"(#version 300 es
precision mediump float;in vec2 t;uniform sampler2D material;uniform vec4 factor;uniform vec3 sheen;uniform int alphaMode;uniform float alphaCutoff;out vec4 color;
void main(){vec4 texel=texture(material,t);color=texel*factor;color.rgb=clamp(color.rgb+texel.rgb*sheen*0.8,0.0,1.0);if(alphaMode==1&&color.a<alphaCutoff)discard;if(alphaMode==0)color.a=1.0;if(alphaMode==1)color.rgb*=color.a;})";
