// Compare panel math with the runtime's actual overlay transform and intersection.
// Run beside a scene whose first panel is visible. No input actions are activated.
#include <openvr.h>
#include <array>
#include <cmath>
#include <cstdio>
int main(){
  vr::EVRInitError init;vr::VR_Init(&init,vr::VRApplication_Background);
  if(init!=vr::VRInitError_None){fprintf(stderr,"init=%d\n",int(init));return 1;}
  auto *overlays=vr::VROverlay();vr::VROverlayHandle_t handle=0;
  auto error=overlays->FindOverlay("sf_overlay_engine.panel.1",&handle);
  if(error!=vr::VROverlayError_None){fprintf(stderr,"find=%d\n",int(error));vr::VR_Shutdown();return 2;}
  vr::HmdMatrix34_t transform{};vr::ETrackingUniverseOrigin origin;float width=0;uint32_t w=0,h=0;
  const auto transformError=overlays->GetOverlayTransformAbsolute(handle,&origin,&transform);
  const auto widthError=overlays->GetOverlayWidthInMeters(handle,&width);
  const auto textureError=overlays->GetOverlayTextureSize(handle,&w,&h);
  if(transformError||widthError||textureError||!w||!h){fprintf(stderr,"transform=%d width=%d texture=%d dimensions=%u,%u\n",int(transformError),int(widthError),int(textureError),w,h);vr::VR_Shutdown();return 3;}
  const float height=width*float(h)/w;
  printf("PANEL_RAY geometry width=%.6f height=%.6f pixels=%u,%u origin=%d\n",width,height,w,h,int(origin));
  vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
  vr::VRSystem()->GetDeviceToAbsoluteTrackingPose(origin,0,poses,vr::k_unMaxTrackedDeviceCount);
  if(!poses[0].bPoseIsValid){fprintf(stderr,"head pose unavailable\n");vr::VR_Shutdown();return 4;}
  vr::HmdVector2_t mouseScale{};overlays->GetOverlayMouseScale(handle,&mouseScale);
  vr::VRTextureBounds_t textureBounds{};overlays->GetOverlayTextureBounds(handle,&textureBounds);
  printf("PANEL_RAY mouse_scale=%g,%g texture_bounds=%g,%g,%g,%g\n",mouseScale.v[0],mouseScale.v[1],textureBounds.uMin,textureBounds.vMin,textureBounds.uMax,textureBounds.vMax);
  // These APIs use mouse-coordinate units, not texture UVs. A noninteractive
  // overlay defaults to 1x1, so explicitly use the texture's pixel coordinate scale.
  vr::HmdVector2_t pixelScale{{float(w),float(h)}};
  const auto scaleError=overlays->SetOverlayMouseScale(handle,&pixelScale);
  if(scaleError!=vr::VROverlayError_None){vr::VR_Shutdown();return 6;}
  bool pass=true;
  for(const auto &uv:std::array<std::array<float,2>,5>{{{.2f,.2f},{.8f,.2f},{.5f,.5f},{.2f,.8f},{.8f,.8f}}}){
    vr::HmdMatrix34_t nativePoint{};vr::HmdVector2_t coordinates{{uv[0]*w,(1-uv[1])*h}};
    const auto pointError=overlays->GetTransformForOverlayCoordinates(handle,origin,coordinates,&nativePoint);
    float pointDelta=0;
    for(int row=0;row<3;row++){
      const float expected=transform.m[row][3]+transform.m[row][0]*(uv[0]-.5f)*width+transform.m[row][1]*(.5f-uv[1])*height;
      const float delta=nativePoint.m[row][3]-expected;pointDelta+=delta*delta;
    }
    printf("PANEL_RAY native_point_error=%d point_delta_m=%g\n",int(pointError),std::sqrt(pointDelta));
    vr::VROverlayIntersectionParams_t params{};params.eOrigin=origin;
    float length=0;
    for(int row=0;row<3;row++){
      params.vSource.v[row]=poses[0].mDeviceToAbsoluteTracking.m[row][3];
      const float point=transform.m[row][3]+transform.m[row][0]*(uv[0]-.5f)*width+transform.m[row][1]*(.5f-uv[1])*height;
      params.vDirection.v[row]=point-params.vSource.v[row];length+=params.vDirection.v[row]*params.vDirection.v[row];
    }
    length=std::sqrt(length);for(float &axis:params.vDirection.v)axis/=length;
    vr::VROverlayIntersectionResults_t result{};
    const bool hit=overlays->ComputeOverlayIntersection(handle,&params,&result);
    // The coordinate transform is the independent geometry reference. The
    // runtime's intersection UV output is not assumed to be canvas pixels.
    const bool matches=pointError==vr::VROverlayError_None&&std::sqrt(pointDelta)<1e-4f&&hit;
    printf("PANEL_RAY target=%.3f,%.3f native_intersection_uv=%.6f,%.6f hit=%d geometry_match=%d\n",uv[0],uv[1],result.vUVs.v[0],result.vUVs.v[1],hit,matches);pass&=matches;
  }
  overlays->SetOverlayMouseScale(handle,&mouseScale);
  vr::VR_Shutdown();return pass?0:5;
}
