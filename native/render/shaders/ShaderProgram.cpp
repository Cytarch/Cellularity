#include "NativeShaders.h"

#include <stdexcept>

ShaderProgram::ShaderProgram(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& name)
        : mGl(gl), mProgramHandle(loadShaderProgram(assets, gl, name)) {
}

void ShaderProgram::use() {
    mGl.useProgram(mProgramHandle);
}

int ShaderProgram::getUniform(const std::string& name) {
    return mGl.getUniformLocation(mProgramHandle, name);
}

int ShaderProgram::getAttrib(const std::string& name) {
    return mGl.getAttribLocation(mProgramHandle, name);
}

int ShaderProgram::loadShaderProgram(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& name) {
    int vertexShader = compileShader(assets, gl, name, gl.vertexShader(), "shaders/vertex/" + name + ".glsl");
    int fragmentShader = compileShader(assets, gl, name, gl.fragmentShader(), "shaders/fragment/" + name + ".glsl");
    int program = gl.createProgram();
    gl.attachShader(program, vertexShader);
    gl.attachShader(program, fragmentShader);
    gl.linkProgram(program);

    if (gl.getProgramParameter(program, gl.linkStatus()) == 0) {
        std::string log = gl.getProgramInfoLog(program);
        gl.deleteProgram(program);
        throw std::runtime_error("Could not link shader program '" + name + "': " + log);
    }

    gl.deleteShader(vertexShader);
    gl.deleteShader(fragmentShader);
    return program;
}

int ShaderProgram::compileShader(const NativeRenderAssets& assets, NativeGlApi& gl, const std::string& shaderName, int type, const std::string& path) {
    int shader = gl.createShader(type);
    gl.shaderSource(shader, assets.readText(path));
    gl.compileShader(shader);

    if (gl.getShaderParameter(shader, gl.compileStatus()) == 0) {
        std::string log = gl.getShaderInfoLog(shader);
        gl.deleteShader(shader);
        throw std::runtime_error("Could not compile shader '" + shaderName + "/" + path + "': " + log);
    }

    return shader;
}
