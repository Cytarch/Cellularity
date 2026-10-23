#pragma once

#include <string>
#include <vector>

class NativeRenderAssets {
public:
    virtual ~NativeRenderAssets() = default;
    virtual std::string readText(const std::string& path) const = 0;
    virtual std::vector<unsigned char> readBytes(const std::string& path) const = 0;
};
