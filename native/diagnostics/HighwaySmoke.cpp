#include "hwy/highway.h"

namespace hn = hwy::HWY_NAMESPACE;

extern "C" HWY_ATTR float cellularityHighwaySmokeValue() {
    const hn::ScalableTag<float> tag;
    const auto value = hn::Set(tag, 1.0f);
    return hn::GetLane(value);
}
