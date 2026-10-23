#pragma once

#include <array>
#include <string>
#include <vector>

#include "render/NativeRenderAssets.h"
#include "render/gl/NativeGlApi.h"

class ShaderProgram {
public:
    ShaderProgram(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& name);
    virtual ~ShaderProgram() = default;

protected:
    NativeGlApi& mGl;
    int mProgramHandle = 0;

    void use();
    int getUniform(const std::string& name);
    int getAttrib(const std::string& name);

private:
    static int loadShaderProgram(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& name);
    static int compileShader(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& shaderName, int type, const std::string& path);
};

class CellShader : public ShaderProgram {
public:
    CellShader(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& shader);

    void begin(const std::array<float, 16>& mvpMatrix, int textureId);
    void drawElementsVbo(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
            int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
            int indexBuffer, int indexOffsetBytes, int numIndices, int centerSize);
    void drawArraysVbo(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
            int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
            int numVertices, int centerSize);
    void end();

private:
    int aPosition = -1;
    int aTC = -1;
    int aCenter = -1;
    int aColor = -1;
    int uMVPMatrix = -1;
    int uTex1 = -1;

    void bindPointerOffsets(int positionBuffer, int positionOffsetBytes, int tcBuffer, int tcOffsetBytes,
            int centerBuffer, int centerOffsetBytes, int colorBuffer, int colorOffsetBytes,
            int centerSize);
};

class LinkShader : public ShaderProgram {
public:
    LinkShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void begin(const std::array<float, 16>& mvpMatrix);
    void drawChunkVbo(int vertexBuffer, int vertexOffsetBytes,
            int patternPositionBuffer, int patternPositionOffsetBytes,
            int numVertices);
    void end();

private:
    int aLinkVertex = -1;
    int aLinkPatternPosition = -1;
    int uMVPMatrix = -1;
};

class FoodShader : public ShaderProgram {
public:
    FoodShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void begin(const std::array<float, 16>& mvpMatrix);
    void drawInstancedVbo(int positionBuffer, int instanceBuffer, int colorBuffer, int instanceCount);
    void end();

private:
    int aPosition = -1;
    int aInstance = -1;
    int aColor = -1;
    int uMVPMatrix = -1;
};

class DeadSplashShader : public ShaderProgram {
public:
    DeadSplashShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void begin(const std::array<float, 16>& mvpMatrix, int textureId);
    void drawInstancedVbo(int positionBuffer, int instanceBuffer, int colorBuffer, int instanceCount);
    void end();

private:
    int aPosition = -1;
    int aInstance = -1;
    int aColor = -1;
    int uMVPMatrix = -1;
    int uTex1 = -1;
};

class BackgroundShader : public ShaderProgram {
public:
    BackgroundShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void draw(const std::array<float, 16>& mvpMatrix, double lightAngle, double lightRange,
            double lightIntensity, double radius, const std::array<float, 3>& backgroundColor, double innerRadius = 0.0);

private:
    int aPosition = -1;
    int uMVPMatrix = -1;
    int uRadius = -1;
    int uDirection = -1;
    int uBackgroundColor = -1;
    int uInnerRadiusSquared = -1;
};

class DensityFieldShader : public ShaderProgram {
public:
    DensityFieldShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void draw(const std::array<float, 16>& mvpMatrix,
            int fieldType,
            double base,
            double strength,
            double angle,
            double offset,
            double softness,
            double radius,
            double displayMin,
            double displayMax);

private:
    int aPosition = -1;
    int uMVPMatrix = -1;
    int uRadius = -1;
    int uField = -1;
    int uFieldExtra = -1;
    int uDisplayRange = -1;
};

class GenomeGridShader : public ShaderProgram {
public:
    GenomeGridShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void draw(const std::array<float, 16>& mvpMatrix,
            float minX,
            float maxX,
            float minY,
            float maxY,
            double worldRadius,
            int gridSize);

private:
    int aPosition = -1;
    int uMVPMatrix = -1;
    int uWorldRadius = -1;
    int uGridSize = -1;
};

class SolidColorShader : public ShaderProgram {
public:
    SolidColorShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void draw(const std::array<float, 16>& mvpMatrix, const std::vector<float>& vertices);
    void draw(const std::array<float, 16>& mvpMatrix,
            const std::vector<float>& vertices,
            const std::array<float, 4>& color);
    void drawVbo(const std::array<float, 16>& mvpMatrix,
            int vertexBuffer,
            int vertexOffsetBytes,
            int vertexCount,
            const std::array<float, 4>& color);

private:
    int aPosition = -1;
    int uMVPMatrix = -1;
    int uColor = -1;
};

class PixelTextShader : public ShaderProgram {
public:
    PixelTextShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void configureVertexArray(int instanceBuffer);
    void drawInstancedVertexArray(const std::array<float, 16>& mvpMatrix,
            int vertexArray,
            int instanceCount,
            int glyphAtlasTexture);

private:
    int aRect = -1;
    int aGlyph = -1;
    int aColor = -1;
    int uMVPMatrix = -1;
    int uGlyphAtlas = -1;
};

class MarkerShader : public ShaderProgram {
public:
    MarkerShader(const NativeRenderAssets& assets, NativeGlApi& gl);

    void draw(const std::array<float, 16>& mvpMatrix, float x, float y, float radius, float alpha);

private:
    int aPosition = -1;
    int aCenter = -1;
    int uMVPMatrix = -1;
};
