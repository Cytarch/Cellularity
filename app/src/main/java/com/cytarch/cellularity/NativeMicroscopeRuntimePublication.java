package com.cytarch.cellularity;

/** One coherent immutable publication copied from the native owner snapshot. */
public final class NativeMicroscopeRuntimePublication {
    private final long[] status;
    private final long[] rateTelemetry;
    private final byte[] environmentState;
    private final byte[] selectedCellState;
    private final byte[] selectedGenomeState;

    public NativeMicroscopeRuntimePublication(
            long[] status,
            long[] rateTelemetry,
            byte[] environmentState,
            byte[] selectedCellState,
            byte[] selectedGenomeState) {
        this.status = status;
        this.rateTelemetry = rateTelemetry;
        this.environmentState = environmentState;
        this.selectedCellState = selectedCellState;
        this.selectedGenomeState = selectedGenomeState;
    }

    public long[] getStatus() {
        return status;
    }

    public long[] getRateTelemetry() {
        return rateTelemetry;
    }

    public byte[] getEnvironmentState() {
        return environmentState;
    }

    public byte[] getSelectedCellState() {
        return selectedCellState;
    }

    public byte[] getSelectedGenomeState() {
        return selectedGenomeState;
    }
}
