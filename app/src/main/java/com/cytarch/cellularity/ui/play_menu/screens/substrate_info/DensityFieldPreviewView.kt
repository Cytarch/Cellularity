package com.cytarch.cellularity.ui.play_menu.screens.substrate_info

import android.annotation.SuppressLint
import android.content.Context
import android.view.MotionEvent
import android.view.View
import android.view.ViewGroup
import android.widget.FrameLayout
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.Composable
import androidx.compose.runtime.key
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.findViewTreeLifecycleOwner
import com.cytarch.cellularity.platform.render.RenderBackendKind
import com.cytarch.cellularity.platform.render.RenderFrameResultCodec
import com.cytarch.cellularity.platform.render.preview.NativeEnvironmentFieldPreviewSession
import com.cytarch.cellularity.platform.render.preview.OnDemandRenderLifecycle
import com.cytarch.cellularity.platform.render.preview.OnDemandRenderSurfaceHostFactory
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanRenderWorkerBridge
import com.cytarch.cellularity.platform.render.vulkan.NativeVulkanWorkerStatus
import com.cytarch.cellularity.shared.model.EnvironmentFieldType
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import com.cytarch.cellularity.shared.ui.play.substrate_info.DensityFieldValueType
import com.cytarch.cellularity.shared.ui.play.substrate_info.EnvironmentFieldTarget
import com.cytarch.cellularity.shared.ui.play.substrate_info.SubstrateFieldUiState
import kotlin.math.PI
import kotlin.math.atan2
import kotlin.math.hypot

@Composable
fun DensityFieldPreview(
    isVisible: Boolean,
    target: EnvironmentFieldTarget,
    fieldState: SubstrateFieldUiState,
    backendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit,
    onValueChanged: (DensityFieldValueType, Double) -> Unit
) {
    require(fieldState.target == target) {
        "Density preview received a mismatched field slice"
    }
    key(backendKind) {
        AndroidView(
            modifier = Modifier
                .fillMaxWidth()
                .height(190.dp),
            factory = { context ->
                DensityFieldPreviewView(
                    context,
                    backendKind,
                    onRenderBackendFailure
                )
            },
            update = { view ->
                view.setGestureCallback(onValueChanged)
                view.update(fieldState)
                view.setPreviewVisible(isVisible)
            },
            onRelease = DensityFieldPreviewView::release
        )
    }
}

@SuppressLint("ViewConstructor")
internal class DensityFieldPreviewView(
    context: Context,
    backendKind: RenderBackendKind,
    onRenderBackendFailure: (String) -> Unit
) : FrameLayout(context) {
    private val previewSession = NativeEnvironmentFieldPreviewSession(
        context.applicationContext,
        PREVIEW_DISH_RADIUS
    )
    private val renderEndpoint = previewSession.renderEndpoint.apply {
        setViewportCamera(0.0f, 0.0f, PREVIEW_CAMERA_ZOOM)
    }

    @Volatile
    private var previewSurfaceReady = false

    private var renderThreadResumed = false
    private var previewVisible = true
    private var hostLifecycleResumed = true
    private var hostLifecycleOwner: LifecycleOwner? = null
    @Volatile
    private var released = false
    private var gestureCallback: ((DensityFieldValueType, Double) -> Unit)? = null
    private var activeGestureState = FieldGestureState(ScalarFieldType.LINEAR_DIRECTIONAL)

    private val hostLifecycleObserver = object : DefaultLifecycleObserver {
        override fun onResume(owner: LifecycleOwner) {
            hostLifecycleResumed = true
            resumeRenderThreadIfReady()
        }

        override fun onPause(owner: LifecycleOwner) {
            hostLifecycleResumed = false
            pauseRenderThread()
        }

        override fun onDestroy(owner: LifecycleOwner) {
            hostLifecycleResumed = false
            pauseRenderThread()
        }
    }

    private val surfaceLifecycle = object : OnDemandRenderLifecycle {
        override fun onSurfaceCreated() {
            if (released) return
            renderEndpoint.onSurfaceCreated()
            if (backendKind == RenderBackendKind.OPEN_GL_ES) {
                renderEndpoint.preparePreviewSurfaceResources()
            }
            previewSurfaceReady = true
        }

        override fun onSurfaceChanged(width: Int, height: Int) {
            if (!released) {
                renderEndpoint.setViewportSurfaceSize(width, height)
            }
        }

        override fun onSurfaceDestroyed() {
            previewSurfaceReady = false
            if (!released) renderEndpoint.onSurfaceDestroyed()
        }

        override fun onRenderFrame(): Long =
            if (!released && previewSurfaceReady) {
                previewSession.draw()
            } else {
                RenderFrameResultCodec.pack(
                    RenderFrameResultCodec.OUTCOME_SKIPPED_NOT_READY,
                    0
                )
            }

        override fun createVulkanRenderWorker(
            context: Context,
            statusReceiver: (NativeVulkanWorkerStatus, Int) -> Unit
        ): NativeVulkanRenderWorkerBridge = previewSession.borrowVulkanEndpoint().use { endpoint ->
            NativeVulkanRenderWorkerBridge.createPreview(
                context = context,
                contentKind = NativeVulkanRenderWorkerBridge.CONTENT_ENVIRONMENT_FIELD_PREVIEW,
                endpointHandle = endpoint.handle,
                endpointGeneration = endpoint.generation,
                statusReceiver = statusReceiver
            )
        }

        override fun onVulkanPlatformSurfaceAvailable() {
            if (!released) previewSurfaceReady = true
        }
    }

    private val renderSurface = OnDemandRenderSurfaceHostFactory.create(
        context = context,
        backendKind = backendKind,
        lifecycle = surfaceLifecycle,
        opaqueSurface = true,
        onSurfaceInvalidated = { previewSurfaceReady = false },
        onRuntimeFailure = onRenderBackendFailure
    )

    init {
        addView(
            renderSurface.view,
            LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT
            )
        )
    }

    override fun onAttachedToWindow() {
        super.onAttachedToWindow()
        bindHostLifecycle()
        resumeRenderThreadIfReady()
    }

    override fun onDetachedFromWindow() {
        pauseRenderThread()
        unbindHostLifecycle()
        super.onDetachedFromWindow()
    }

    fun setGestureCallback(callback: (DensityFieldValueType, Double) -> Unit) {
        gestureCallback = callback
    }

    fun update(fieldState: SubstrateFieldUiState) {
        if (released) return
        activeGestureState = FieldGestureState(fieldState.type)
        previewSession.publish(fieldState)
        if (renderThreadResumed) renderSurface.requestFrame()
    }

    fun setPreviewVisible(isVisible: Boolean) {
        if (released || previewVisible == isVisible) return
        previewVisible = isVisible
        visibility = if (isVisible) View.VISIBLE else View.GONE
        if (isVisible) {
            resumeRenderThreadIfReady()
        } else {
            pauseRenderThread()
        }
    }

    fun release() {
        if (released) return
        released = true
        pauseRenderThread()
        previewVisible = false
        previewSurfaceReady = false
        gestureCallback = null
        unbindHostLifecycle()
        renderSurface.release()
        previewSession.close()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        if (released || !previewVisible) return false
        val action = event.actionMasked
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_MOVE) {
            parent?.requestDisallowInterceptTouchEvent(true)
            return handleGesture(event)
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_CANCEL) {
            parent?.requestDisallowInterceptTouchEvent(false)
            performClick()
            return true
        }
        return true
    }

    override fun performClick(): Boolean {
        super.performClick()
        return true
    }

    private fun bindHostLifecycle() {
        val owner = findViewTreeLifecycleOwner() ?: return
        if (hostLifecycleOwner === owner) return
        unbindHostLifecycle()
        hostLifecycleOwner = owner
        owner.lifecycle.addObserver(hostLifecycleObserver)
        hostLifecycleResumed = owner.lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)
        if (!hostLifecycleResumed) pauseRenderThread()
    }

    private fun unbindHostLifecycle() {
        hostLifecycleOwner?.lifecycle?.removeObserver(hostLifecycleObserver)
        hostLifecycleOwner = null
    }

    private fun resumeRenderThreadIfReady() {
        if (
            released ||
            renderThreadResumed ||
            !previewVisible ||
            !hostLifecycleResumed ||
            !isAttachedToWindow
        ) {
            return
        }
        renderThreadResumed = true
        renderSurface.resumeRendering()
    }

    private fun pauseRenderThread() {
        if (!renderThreadResumed) return
        renderThreadResumed = false
        previewSurfaceReady = false
        renderSurface.pauseRendering()
    }

    private fun handleGesture(event: MotionEvent): Boolean {
        val callback = gestureCallback ?: return false
        val activeField = activeGestureState
        val centerX = width * 0.5f
        val centerY = height * 0.5f
        if (width <= 0 || height <= 0) return false
        if (activeField.type is VectorFieldType) {
            if (activeField.type == VectorFieldType.LINEAR_DIRECTIONAL) {
                val angle = atan2(
                    (centerY - event.y).toDouble(),
                    (event.x - centerX).toDouble()
                ).coerceToPiRange()
                callback(DensityFieldValueType.ANGLE, angle)
                return true
            }
            return true
        }
        when (activeField.type as ScalarFieldType) {
            ScalarFieldType.LINEAR_DIRECTIONAL -> {
                val angle = atan2(
                    (centerY - event.y).toDouble(),
                    (event.x - centerX).toDouble()
                ).coerceToPiRange()
                callback(DensityFieldValueType.ANGLE, angle)
                return true
            }
            ScalarFieldType.RADIAL -> {
                val radius = minOf(width, height) * 0.5f
                if (radius <= 0f) return false
                val distance = hypot(
                    (event.x - centerX).toDouble(),
                    (event.y - centerY).toDouble()
                )
                callback(
                    DensityFieldValueType.OFFSET,
                    (distance / radius).coerceIn(0.0, 1.0)
                )
                return true
            }
            ScalarFieldType.CONSTANT -> return false
        }
    }

    private fun Double.coerceToPiRange(): Double {
        var value = this
        val twoPi = PI * 2.0
        while (value <= -PI) value += twoPi
        while (value > PI) value -= twoPi
        return value
    }

    private companion object {
        const val PREVIEW_DISH_RADIUS = 1.0
        val PREVIEW_CAMERA_ZOOM = (1.0 / (PREVIEW_DISH_RADIUS * 1.1)).toFloat()
    }
}

private data class FieldGestureState(
    val type: EnvironmentFieldType
)
