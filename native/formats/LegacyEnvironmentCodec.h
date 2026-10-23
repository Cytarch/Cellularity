#pragma once

#include "core/Environment.h"

class JavaObjectStreamReader;

namespace cellularity::snapshot_codec {

Environment readLegacyEnvironment(JavaObjectStreamReader& input,
        bool previewMode,
        Environment environment);

} // namespace cellularity::snapshot_codec
