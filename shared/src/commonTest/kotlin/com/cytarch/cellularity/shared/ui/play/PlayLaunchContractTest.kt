package com.cytarch.cellularity.shared.ui.play

import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.model.SimulationContract
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertIs

class PlayLaunchContractTest {
    @Test
    fun storedLaunchProducesTypedIdAndIgnoresNewWorldNumbers() {
        val result = PlayLaunchContract.decode(
            stored = true,
            name = "Saved plate",
            maxCells = -1,
            maxFoods = -1,
            radius = Double.NaN
        )

        assertEquals(
            PlayLaunchSource.Stored(StoredSubstrateId("Saved plate")),
            assertIs<PlayLaunchDecodeResult.Valid>(result).source
        )
    }

    @Test
    fun missingOrPathLikeStoredNameIsRejectedWithoutThrowing() {
        listOf<String?>(null, "", "../plate", "folder/plate").forEach { name ->
            val result = PlayLaunchContract.decode(
                stored = true,
                name = name,
                maxCells = 1,
                maxFoods = 0,
                radius = 1.0
            )

            assertEquals(
                PlayLaunchFailureReason.BAD_STORED_ID,
                assertIs<PlayLaunchDecodeResult.Invalid>(result).reason
            )
        }
    }

    @Test
    fun supportedNewWorldConfigurationIsPreservedExactly() {
        val configuration = SimulationContract.defaultConfiguration.copy(
            maxCellCount = 2_000,
            maxFoodCount = 4_000,
            radius = 2.5,
            innerRadius = 1.0
        )

        val result = PlayLaunchContract.decode(
            stored = false,
            name = "ignored",
            maxCells = configuration.maxCellCount,
            maxFoods = configuration.maxFoodCount,
            radius = configuration.radius,
            innerRadius = configuration.innerRadius
        )

        assertEquals(
            PlayLaunchSource.New(configuration),
            assertIs<PlayLaunchDecodeResult.Valid>(result).source
        )
    }

    @Test
    fun invalidOrOversizedNewWorldConfigurationIsRejected() {
        listOf(
            Triple(0, 0, 1.0),
            Triple(1, -1, 1.0),
            Triple(1, 0, Double.NaN),
            Triple(Int.MAX_VALUE, Int.MAX_VALUE, 1.0)
        ).forEach { (cells, foods, radius) ->
            val result = PlayLaunchContract.decode(
                stored = false,
                name = null,
                maxCells = cells,
                maxFoods = foods,
                radius = radius
            )

            assertEquals(
                PlayLaunchFailureReason.UNSUPPORTED_CONFIGURATION,
                assertIs<PlayLaunchDecodeResult.Invalid>(result).reason
            )
        }
    }
}
