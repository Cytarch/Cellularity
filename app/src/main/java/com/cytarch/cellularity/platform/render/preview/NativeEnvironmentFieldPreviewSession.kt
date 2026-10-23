package com.cytarch.cellularity.platform.render.preview

import android.content.Context
import com.cytarch.cellularity.NativeSimulationFacade
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateFieldUiState

/** Narrow owner for one native Environment Field Preview world. */
internal class NativeEnvironmentFieldPreviewSession(
    context: Context,
    dishRadius: Double
) : AutoCloseable {
    private val facade = NativeSimulationFacade(
        context.applicationContext,
        true,
        1,
        0,
        dishRadius
    )
    private val updateResult =
        EnvironmentFieldPreviewUpdateResultLayout.allocateBuffer()

    val renderEndpoint = facade.microscopeRenderEndpoint()
    val endpointGeneration: Long = facade.environmentFieldPreviewEndpointGeneration()

    fun borrowVulkanEndpoint(): NativePreviewEndpointOwner.Borrow =
        facade.borrowEnvironmentFieldPreviewEndpoint()

    fun publish(state: SubstrateFieldUiState) {
        val update = state.toEnvironmentFieldPreviewUpdate()
        facade.publishEnvironmentFieldPreviewState(
            update.targetWire,
            update.fieldTypeWire,
            update.base,
            update.strength,
            update.angle,
            update.offset,
            update.softness,
            updateResult
        )
        val result = EnvironmentFieldPreviewUpdateResultLayout.decode(
            updateResult
        )
        check(result.accepted) {
            "Environment Field Preview update was rejected: ${result.status}"
        }
    }

    fun draw(): Long = facade.drawEnvironmentFieldPreview()

    override fun close() = facade.close()
}
