#pragma once

#include <array>
#include <cstdint>

class NativeViewport {
public:
    struct ViewBounds {
        float minX = 0.0f;
        float maxX = 0.0f;
        float minY = 0.0f;
        float maxY = 0.0f;
    };

    explicit NativeViewport(float radius);

    void setRadius(float radius);
    void setSurfaceSize(int width, int height);
    void setCamera(float x, float y, float zoom);
    void setCameraBoundsEnabled(bool enabled);
    void moveByScreenDelta(float dx, float dy);
    void zoomAt(float factor, float screenX, float screenY);
    void updateZoom();

    float screenToWorldX(float screenX) const;
    float screenToWorldY(float screenY) const;
    std::array<float, 16> worldMatrix(const std::array<float, 16>& surfaceMatrix) const;
    ViewBounds viewBounds(float margin) const;

    float cameraX() const;
    float cameraY() const;
    float zoom() const;
    int surfaceWidth() const;
    int surfaceHeight() const;
    float viewWidth() const;
    float viewHeight() const;
    float pixelPerWorldCoord() const;
    std::uint64_t revision() const;
    bool isReady() const;

private:
    static constexpr float MAX_ZOOM = 300000.0f;

    float mRadius;
    int mSurfaceWidth = 0;
    int mSurfaceHeight = 0;
    float mCameraX = 0.0f;
    float mCameraY = 0.0f;
    float mZoom = 0.0f;
    float mViewWidth = 0.0f;
    float mViewHeight = 0.0f;
    float mPixelPerWorldCoord = 0.0f;
    std::uint64_t mRevision = 1;
    bool mIsReady = false;
    bool mCameraBoundsEnabled = true;

    void advanceRevision();
};
