package com.cytarch.cellularity;

import com.cytarch.cellularity.shared.model.CellularityErrorCode;

/** Typed failure produced while reading external or native wire data. */
public final class CellularityDataException extends RuntimeException {
    private final CellularityErrorCode code;

    public CellularityDataException(String message) {
        this(CellularityErrorCode.INVALID_VALUE, message);
    }

    public CellularityDataException(String code, String message) {
        this(CellularityErrorCode.valueOf(code), message);
    }

    public CellularityDataException(CellularityErrorCode code, String message) {
        super(message);
        this.code = code;
    }

    public CellularityErrorCode getCode() {
        return code;
    }
}
