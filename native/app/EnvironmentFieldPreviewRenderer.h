#pragma once

class NativeCellRendererResources;
class NativeGlApi;

namespace cellularity::render {
struct EnvironmentFieldPreviewCommand;
} // namespace cellularity::render

class EnvironmentFieldPreviewRenderer {
public:
    int draw(
            const cellularity::render::EnvironmentFieldPreviewCommand& command,
            NativeGlApi& gl,
            NativeCellRendererResources& resources);
};
