package com.cytarch.cellularity.platform.render

/** Android render-host implementations selectable before a surface is created. */
enum class RenderBackendKind {
    OPEN_GL_ES,
    VULKAN
}
