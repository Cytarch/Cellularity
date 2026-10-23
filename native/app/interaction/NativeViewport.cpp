#include "app/interaction/NativeViewport.h"

#include <algorithm>
#include <cmath>
#include <limits>

NativeViewport::NativeViewport(float radius)
        : mRadius(radius),
          mZoom(radius > 0.0f ? 1.0f / radius : 1.0f) {
}

void NativeViewport::setRadius(float radius) {
    mRadius = radius;
    updateZoom();
}

void NativeViewport::setSurfaceSize(int width, int height) {
    mSurfaceWidth = width;
    mSurfaceHeight = height;
    mIsReady = width > 0 && height > 0;
    updateZoom();
}

void NativeViewport::setCamera(float x, float y, float zoom) {
    mCameraX = x;
    mCameraY = y;
    mZoom = zoom;
    updateZoom();
}

void NativeViewport::setCameraBoundsEnabled(bool enabled) {
    mCameraBoundsEnabled = enabled;
    updateZoom();
}

void NativeViewport::moveByScreenDelta(float dx, float dy) {
    if (!mIsReady || mPixelPerWorldCoord == 0.0f) {
        return;
    }
    mCameraX -= dx / mPixelPerWorldCoord;
    mCameraY += dy / mPixelPerWorldCoord;
    updateZoom();
}

void NativeViewport::zoomAt(float factor, float screenX, float screenY) {
    if (!mIsReady) {
        return;
    }
    float worldX = screenToWorldX(screenX);
    float worldY = screenToWorldY(screenY);
    mZoom *= factor;
    mCameraX += (worldX - mCameraX) * (factor - 1.0f);
    mCameraY += (worldY - mCameraY) * (factor - 1.0f);
    updateZoom();
}

void NativeViewport::updateZoom() {
    advanceRevision();
    if (mRadius > 0.0f) {
        mZoom = std::max(mZoom, 1.0f / mRadius);
    }
    mZoom = std::min(mZoom, MAX_ZOOM);

    if (!mIsReady) {
        return;
    }

    float minSide = static_cast<float>(std::min(mSurfaceWidth, mSurfaceHeight));
    mPixelPerWorldCoord = mZoom * (minSide / 2.0f);
    if (mPixelPerWorldCoord == 0.0f) {
        mViewWidth = 0.0f;
        mViewHeight = 0.0f;
        return;
    }

    mViewWidth = static_cast<float>(mSurfaceWidth) / mPixelPerWorldCoord;
    mViewHeight = static_cast<float>(mSurfaceHeight) / mPixelPerWorldCoord;

    if (!mCameraBoundsEnabled) {
        return;
    }

    float limitX = std::abs(-mRadius + mViewWidth / 2.0f);
    float limitY = std::abs(-mRadius + mViewHeight / 2.0f);
    mCameraX = std::max(-limitX, std::min(limitX, mCameraX));
    mCameraY = std::max(-limitY, std::min(limitY, mCameraY));
}

float NativeViewport::screenToWorldX(float screenX) const {
    if (mSurfaceWidth == 0) {
        return mCameraX;
    }
    return mCameraX + (screenX / static_cast<float>(mSurfaceWidth) - 0.5f) * mViewWidth;
}

float NativeViewport::screenToWorldY(float screenY) const {
    if (mSurfaceHeight == 0) {
        return mCameraY;
    }
    return mCameraY + (0.5f - screenY / static_cast<float>(mSurfaceHeight)) * mViewHeight;
}

std::array<float, 16> NativeViewport::worldMatrix(const std::array<float, 16>& surfaceMatrix) const {
    std::array<float, 16> viewMatrix{
            2.0f / mViewWidth, 0.0f, 0.0f, 0.0f,
            0.0f, 2.0f / mViewHeight, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            -2.0f * mCameraX / mViewWidth, -2.0f * mCameraY / mViewHeight, 0.0f, 1.0f
    };

    std::array<float, 16> result{};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                result[i + 4 * j] += viewMatrix[k + 4 * j] * surfaceMatrix[i + 4 * k];
            }
        }
    }
    return result;
}

NativeViewport::ViewBounds NativeViewport::viewBounds(float margin) const {
    float halfWidth = mViewWidth / 2.0f + margin;
    float halfHeight = mViewHeight / 2.0f + margin;
    return {mCameraX - halfWidth, mCameraX + halfWidth, mCameraY - halfHeight, mCameraY + halfHeight};
}

float NativeViewport::cameraX() const {
    return mCameraX;
}

float NativeViewport::cameraY() const {
    return mCameraY;
}

float NativeViewport::zoom() const {
    return mZoom;
}

int NativeViewport::surfaceWidth() const {
    return mSurfaceWidth;
}

int NativeViewport::surfaceHeight() const {
    return mSurfaceHeight;
}

float NativeViewport::viewWidth() const {
    return mViewWidth;
}

float NativeViewport::viewHeight() const {
    return mViewHeight;
}

float NativeViewport::pixelPerWorldCoord() const {
    return mPixelPerWorldCoord;
}

std::uint64_t NativeViewport::revision() const {
    return mRevision;
}

bool NativeViewport::isReady() const {
    return mIsReady;
}

void NativeViewport::advanceRevision() {
    if (mRevision == std::numeric_limits<std::uint64_t>::max()) {
        mRevision = 1;
    } else {
        ++mRevision;
    }
}
