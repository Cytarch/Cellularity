#include "render/NativeCellRendererResources.h"

#include <stdexcept>

#include "render/NativeRenderTextureLoader.h"
#include "render/gl/NativeGlApi.h"

namespace {

int filterValue(NativeGlApi& gl, NativeRenderTextureFilter filter) {
    return filter == NativeRenderTextureFilter::Nearest ? gl.nearest()
                                                        : gl.linear();
}

int wrapValue(NativeGlApi& gl, NativeRenderTextureWrap wrap) {
    return wrap == NativeRenderTextureWrap::ClampToEdge ? gl.clampToEdge()
                                                        : gl.repeat();
}

int createGlTexture(NativeGlApi& gl,
        const NativeRenderTexturePayload& payload,
        const char* description) {
    if (!payload.valid()) {
        throw std::runtime_error(
                std::string("Invalid decoded renderer texture: ")
                + description);
    }
    int texture = 0;
    gl.genTextures(1, &texture);
    if (texture == 0) {
        throw std::runtime_error(
                std::string("Could not create renderer texture: ")
                + description);
    }
    try {
        gl.bindTexture(gl.texture2d(), texture);
        gl.textureParameter(gl.texture2d(), gl.textureMinFilter(),
                filterValue(gl, payload.minFilter));
        gl.textureParameter(gl.texture2d(), gl.textureMagFilter(),
                filterValue(gl, payload.magFilter));
        gl.textureParameter(gl.texture2d(), gl.textureWrapS(),
                wrapValue(gl, payload.wrapS));
        gl.textureParameter(gl.texture2d(), gl.textureWrapT(),
                wrapValue(gl, payload.wrapT));
        gl.pixelStore(gl.unpackAlignment(), 1);
        const bool red = payload.pixelFormat
                == NativeRenderTexturePixelFormat::R8;
        gl.textureImage2d(gl.texture2d(), 0,
                red ? gl.r8() : gl.rgba8(),
                static_cast<int>(payload.width),
                static_cast<int>(payload.height), 0,
                red ? gl.red() : gl.rgba(), gl.unsignedByte(),
                payload.pixels.data());
        gl.pixelStore(gl.unpackAlignment(), 4);
        return texture;
    } catch (...) {
        gl.deleteTextures(1, &texture);
        throw;
    }
}

} // namespace

NativeCellRendererResources::NativeCellRendererResources(const NativeRenderAssets& assets,
        NativeGlApi& gl,
        NativeRenderTextureLoader& textureLoader,
        bool isPreviewMode)
        : mIsPreviewMode(isPreviewMode),
          mGl(&gl) {
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length; cellTypeIndex++) {
        CellTypeInfo info = CellTypes::info(CellTypes::fromOrdinal(cellTypeIndex));
        if (!info.baseShader.empty()) {
            mBasicCellShaders[cellTypeIndex] = std::make_unique<CellShader>(assets, gl, std::string(info.baseShader));
        }
        if (!info.outlineShader.empty()) {
            mOutlineCellShaders[cellTypeIndex] = std::make_unique<CellShader>(assets, gl, std::string(info.outlineShader));
        }
    }

    if (mIsPreviewMode) {
        mArrowShader = std::make_unique<CellShader>(assets, gl, "arrow");
        mPixelTextShader = std::make_unique<PixelTextShader>(assets, gl);
    }

    mFoodShader = std::make_unique<FoodShader>(assets, gl);
    mDeadSplashShader = std::make_unique<DeadSplashShader>(assets, gl);
    mLinkShader = std::make_unique<LinkShader>(assets, gl);
    mBackgroundShader = std::make_unique<BackgroundShader>(assets, gl);
    mDensityFieldShader = std::make_unique<DensityFieldShader>(assets, gl);
    mGenomeGridShader = std::make_unique<GenomeGridShader>(assets, gl);
    mSolidColorShader = std::make_unique<SolidColorShader>(assets, gl);
#ifdef CELLULARITY_PERFORMANCE_HUD
    if (mPixelTextShader == nullptr) {
        mPixelTextShader = std::make_unique<PixelTextShader>(assets, gl);
    }
#endif
    mMarkerShader = std::make_unique<MarkerShader>(assets, gl);
    mDotTextureHandle = createGlTexture(
            gl, textureLoader.loadTexture("small_dots", false),
            "small_dots");
    try {
        mBesselTextureHandle = createGlTexture(
                gl,
                textureLoader.loadTexture("gauss_bessel_integral", true),
                "gauss_bessel_integral");
    } catch (...) {
        releaseTextures();
        throw;
    }
}

NativeCellRendererResources::~NativeCellRendererResources() {
    releaseTextures();
}

std::array<CellShader*, CellTypes::length> NativeCellRendererResources::basicCellShaders() {
    std::array<CellShader*, CellTypes::length> result{};
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length; cellTypeIndex++) {
        result[cellTypeIndex] = mBasicCellShaders[cellTypeIndex].get();
    }
    return result;
}

std::array<CellShader*, CellTypes::length> NativeCellRendererResources::outlineCellShaders() {
    std::array<CellShader*, CellTypes::length> result{};
    for (int cellTypeIndex = 0; cellTypeIndex < CellTypes::length; cellTypeIndex++) {
        result[cellTypeIndex] = mOutlineCellShaders[cellTypeIndex].get();
    }
    return result;
}

CellShader* NativeCellRendererResources::arrowShader() {
    return mArrowShader.get();
}

FoodShader& NativeCellRendererResources::foodShader() {
    return *mFoodShader;
}

DeadSplashShader& NativeCellRendererResources::deadSplashShader() {
    return *mDeadSplashShader;
}

LinkShader& NativeCellRendererResources::linkShader() {
    return *mLinkShader;
}

BackgroundShader& NativeCellRendererResources::backgroundShader() {
    return *mBackgroundShader;
}

DensityFieldShader& NativeCellRendererResources::densityFieldShader() {
    return *mDensityFieldShader;
}

GenomeGridShader& NativeCellRendererResources::genomeGridShader() {
    return *mGenomeGridShader;
}

SolidColorShader& NativeCellRendererResources::solidColorShader() {
    return *mSolidColorShader;
}

PixelTextShader& NativeCellRendererResources::pixelTextShader() {
    if (mPixelTextShader == nullptr) {
        throw std::logic_error(
                "Pixel text shader is unavailable outside Preview or performance HUD rendering");
    }
    return *mPixelTextShader;
}

MarkerShader& NativeCellRendererResources::markerShader() {
    return *mMarkerShader;
}

int NativeCellRendererResources::dotTextureHandle() const {
    return mDotTextureHandle;
}

int NativeCellRendererResources::besselTextureHandle() const {
    return mBesselTextureHandle;
}

bool NativeCellRendererResources::isPreviewMode() const {
    return mIsPreviewMode;
}

void NativeCellRendererResources::releaseTextures() noexcept {
    if (mGl == nullptr) {
        mDotTextureHandle = 0;
        mBesselTextureHandle = 0;
        return;
    }
    const int textures[] = {mDotTextureHandle, mBesselTextureHandle};
    for (const int texture : textures) {
        if (texture != 0) {
            mGl->deleteTextures(1, &texture);
        }
    }
    mDotTextureHandle = 0;
    mBesselTextureHandle = 0;
}

void NativeCellRendererResources::abandonTextures() noexcept {
    // The context is already gone, so its object names are invalid and must
    // not be passed to a different or nonexistent GL context by the destructor.
    mGl = nullptr;
    mDotTextureHandle = 0;
    mBesselTextureHandle = 0;
}
