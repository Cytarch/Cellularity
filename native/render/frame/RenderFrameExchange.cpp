#include "render/frame/RenderFrameExchange.h"

namespace cellularity::render {

template class RenderFrameExchange<RenderSourceFrame,
        RenderSourceFrameExchangeTraits,
        kRenderFrameExchangeSlotCount>;
template class RenderFrameExchange<PreparedRenderFrame,
        PreparedRenderFrameExchangeTraits,
        kRenderFrameExchangeSlotCount>;

} // namespace cellularity::render
