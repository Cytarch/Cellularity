#pragma once

#include <array>
#include <memory>

#include "core/CellType.h"
#include "render/shaders/NativeShaders.h"

class NativeGlApi;
class NativeRenderAssets;
class NativeRenderTextureLoader;

class NativeCellRendererResources {
public:
    NativeCellRendererResources(const NativeRenderAssets& assets,
            NativeGlApi& gl,
            NativeRenderTextureLoader& textureLoader,
            bool isPreviewMode);
    ~NativeCellRendererResources();

    std::array<CellShader*, CellTypes::length> basicCellShaders();
    std::array<CellShader*, CellTypes::length> outlineCellShaders();
    CellShader* arrowShader();
    FoodShader& foodShader();
    DeadSplashShader& deadSplashShader();
    LinkShader& linkShader();
    BackgroundShader& backgroundShader();
    DensityFieldShader& densityFieldShader();
    GenomeGridShader& genomeGridShader();
    SolidColorShader& solidColorShader();
    PixelTextShader& pixelTextShader();
    MarkerShader& markerShader();
    int dotTextureHandle() const;
    int besselTextureHandle() const;
    bool isPreviewMode() const;
    void releaseTextures() noexcept;
    void abandonTextures() noexcept;

private:
    bool mIsPreviewMode = false;
    std::array<std::unique_ptr<CellShader>, CellTypes::length> mBasicCellShaders;
    std::array<std::unique_ptr<CellShader>, CellTypes::length> mOutlineCellShaders;
    std::unique_ptr<CellShader> mArrowShader;
    std::unique_ptr<FoodShader> mFoodShader;
    std::unique_ptr<DeadSplashShader> mDeadSplashShader;
    std::unique_ptr<LinkShader> mLinkShader;
    std::unique_ptr<BackgroundShader> mBackgroundShader;
    std::unique_ptr<DensityFieldShader> mDensityFieldShader;
    std::unique_ptr<GenomeGridShader> mGenomeGridShader;
    std::unique_ptr<SolidColorShader> mSolidColorShader;
    std::unique_ptr<PixelTextShader> mPixelTextShader;
    std::unique_ptr<MarkerShader> mMarkerShader;
    NativeGlApi* mGl = nullptr;
    int mDotTextureHandle = 0;
    int mBesselTextureHandle = 0;
};
