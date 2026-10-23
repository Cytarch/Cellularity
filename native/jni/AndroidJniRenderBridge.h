#pragma once

#ifdef __ANDROID__

#include "render/gl/AndroidGles30Api.h"

class NativeCellularityController;
class AndroidRenderAssetsLease;

AndroidGles30Api& androidGl();
void loadAndroidRendererResources(
        NativeCellularityController& controller,
        const AndroidRenderAssetsLease& assetsLease);

#endif
