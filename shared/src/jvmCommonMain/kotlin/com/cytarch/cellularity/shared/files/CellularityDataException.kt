/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.shared.files

import com.cytarch.cellularity.shared.model.CellularityErrorCode

/**
 * A typed failure from a codec or the native layer.
 *
 * It lives in the JVM code shared by Android and the desktop because the native bridges on
 * both platforms throw it by class name (native/jni/NativeJniUtils.h, JAVA_DATA_EXCEPTION)
 * with the (String code, String message) constructor, and both file repositories catch it.
 */
class CellularityDataException(
    val code: CellularityErrorCode,
    message: String
) : RuntimeException(message) {

    constructor(message: String) : this(CellularityErrorCode.INVALID_VALUE, message)

    /** Called from native code: the code is the CellularityErrorCode constant name. */
    constructor(code: String, message: String) : this(CellularityErrorCode.valueOf(code), message)
}
