#pragma once
// Frozen point/spot loop before scene directional lights, for pixels and timings.
static const char *directionalLightReferenceLoop=R"GLSL(  for(int i=0;i<8;i++){
   if(i>=pointLightCount)break;
   vec3 delta=pointPositionRange[i].xyz-litPosition;
   float distance2=dot(delta,delta),range=pointPositionRange[i].w;
   if(distance2>=range*range)continue;
   vec3 pl=safeUnit(delta,n),ph=safeUnit(pl+v,n);
   float cone=1.;
   if(spotDirectionOuter[i].w>=-1.){
    float cosine=dot(-pl,spotDirectionOuter[i].xyz),outer=spotDirectionOuter[i].w,inner=spotInner[i];
    if(cosine<outer)continue;
    cone=inner>outer?clamp((cosine-outer)/(inner-outer),0.,1.):step(outer,cosine);
    cone*=cone;
   }
   float pnl=max(dot(n,pl),0.),pnh=max(dot(n,ph),0.),pvh=max(dot(v,ph),0.);
   float pd=pnh*pnh*(a2-1.)+1.,distributionP=a2/(PI*pd*pd);
   float visibilityP=.5/max(pnl*sqrt(nv*nv*(1.-a2)+a2)+nv*sqrt(pnl*pnl*(1.-a2)+a2),1e-6);
   vec3 pf=f0+(1.-f0)*pow(1.-pvh,5.);
   float window=max(1.-pow(distance2/(range*range),2.),0.);
   float attenuation=cone*window*window/max(distance2,.0001);
   result+=((1.-pf)*(1.-metal)*base.rgb/PI+distributionP*visibilityP*pf)*pointColorIntensity[i].rgb*pointColorIntensity[i].w*pnl*attenuation;
  }
)GLSL";
