package com.cytarch.cellularity.shared.ui.play.substrate_info

import com.cytarch.cellularity.shared.model.EnvironmentContract
import com.cytarch.cellularity.shared.model.ScalarFieldType
import com.cytarch.cellularity.shared.model.VectorFieldType
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.jetbrains.compose.resources.StringResource

class SubstrateInfoPresentationTest {
    @Test
    fun environmentAndTabDescriptorsCoverEveryTypedValue() {
        assertEquals(
            EnvironmentFieldTarget.entries.toSet(),
            environmentFieldPresentationDescriptors.map { it.target }.toSet()
        )
        assertEquals(
            ScalarFieldType.entries,
            scalarFieldModePresentationDescriptors.map { it.type }
        )
        assertEquals(
            VectorFieldType.entries,
            vectorFieldModePresentationDescriptors.map { it.type }
        )
        assertEquals(
            DensityFieldValueType.entries,
            environmentFieldComponentPresentationDescriptors.map { it.component }
        )
        assertEquals(
            SubstrateInfoSection.entries,
            substrateInfoTabPresentationDescriptors.map { it.section }
        )

        EnvironmentFieldTarget.entries.forEach { target ->
            val descriptor = target.presentationDescriptor()
            assertEquals(target, descriptor.target)
            descriptor.titleResource.assertExists()
        }
        ScalarFieldType.entries.forEach { type ->
            val descriptor = type.presentationDescriptor()
            assertEquals(type, descriptor.type)
            descriptor.labelResource.assertExists()
        }
        VectorFieldType.entries.forEach { type ->
            val descriptor = type.presentationDescriptor()
            assertEquals(type, descriptor.type)
            descriptor.labelResource.assertExists()
        }
        DensityFieldValueType.entries.forEach { component ->
            val descriptor = component.presentationDescriptor()
            assertEquals(component, descriptor.component)
            descriptor.labelResource.assertExists()
        }
        SubstrateInfoSection.entries.forEach { section ->
            val descriptor = section.presentationDescriptor()
            assertEquals(section, descriptor.section)
            descriptor.titleResource.assertExists()
        }
    }

    @Test
    fun mutationDescriptorsPreserveEveryStableIdAndWireIndex() {
        assertEquals(EnvironmentContract.MUTATION_WEIGHT_COUNT, mutationWeightDescriptors.size)
        assertEquals(
            (0 until EnvironmentContract.MUTATION_WEIGHT_COUNT).toList(),
            mutationWeightDescriptors.map { it.index }
        )
        assertEquals(
            expectedMutationStableIds,
            mutationWeightDescriptors.map { it.stableId }
        )
        assertEquals(
            EnvironmentContract.MUTATION_WEIGHT_COUNT,
            mutationWeightDescriptors.map { it.stableId }.toSet().size
        )
        mutationWeightDescriptors.forEach { descriptor ->
            assertTrue(
                descriptor.stableId.startsWith("gene.") ||
                    descriptor.stableId.startsWith("genome.")
            )
            val resources = descriptor.label.referencedResources()
            assertTrue(resources.isNotEmpty(), descriptor.stableId)
            resources.forEach { it.assertExists(descriptor.stableId) }
        }
    }

    @Test
    fun parametricFamiliesReuseExactlyFiveSuffixDescriptors() {
        val parametricLabels = mutationWeightDescriptors.mapNotNull { descriptor ->
            descriptor.label as? MutationWeightLabelDescriptor.Parametric
        }

        assertEquals(11, mutationParametricFamilyDescriptors.size)
        assertEquals(5, mutationParametricSuffixDescriptors.size)
        assertEquals(55, parametricLabels.size)
        mutationParametricFamilyDescriptors.forEach { family ->
            assertEquals(5, parametricLabels.count { it.family == family }, family.stableSegment)
        }
        mutationParametricSuffixDescriptors.forEach { suffix ->
            assertEquals(11, parametricLabels.count { it.suffix == suffix }, suffix.stableSegment)
        }
        assertEquals(
            mutationParametricSuffixDescriptors.toSet(),
            parametricLabels.map { it.suffix }.toSet()
        )
        assertEquals(
            listOf(1, 2, 3, 4),
            mutationParametricFamilyDescriptors.mapNotNull { it.displayNumber }
        )

        val numberedIntegerLabels = mutationWeightDescriptors
            .filter { it.stableId.startsWith("gene.integer.gen_signal_id_") }
            .map { it.label as MutationWeightLabelDescriptor.Numbered }
        assertEquals(listOf(1, 2, 3, 4), numberedIntegerLabels.map { it.displayNumber })
    }

    @Test
    fun mutationSearchMatchesStableIdAndLocalizedLabelWithoutChangingIdentity() {
        val descriptor = mutationWeightDescriptors.single {
            it.stableId == "gene.continuous.split_angle"
        }

        assertTrue(descriptor.matchesFilter("Division angle", "GENE.CONTINUOUS.SPLIT"))
        assertTrue(descriptor.matchesFilter("Division angle", "angle"))
        assertTrue(descriptor.matchesFilter("Division angle", "  DIVISION  "))
        assertTrue(descriptor.matchesFilter("Division angle", ""))
        assertFalse(descriptor.matchesFilter("Division angle", "density"))
        assertEquals(3, descriptor.index)
        assertEquals("gene.continuous.split_angle", descriptor.stableId)
    }
}

private fun StringResource.assertExists(context: String = key) {
    assertTrue(key.isNotBlank(), context)
}

private val expectedMutationStableIds = listOf(
    "gene.color.hue",
    "gene.color.saturation",
    "gene.color.value",
    "gene.continuous.split_angle",
    "gene.continuous.child1_angle",
    "gene.continuous.child2_angle",
    "gene.continuous.mass_flow_priority",
    "gene.continuous.split_mass",
    "gene.continuous.split_ratio",
    "gene.continuous.link_strength",
    "gene.child_index.child1",
    "gene.child_index.child2",
    "gene.boolean.make_link",
    "gene.boolean.child1_keep_links",
    "gene.boolean.child2_keep_links",
    "gene.boolean.stay_alive",
    "gene.boolean.child1_mirror",
    "gene.boolean.child2_mirror",
    "gene.cell_type",
    "gene.parametric.swim_force.variable_a",
    "gene.parametric.swim_force.variable_b",
    "gene.parametric.swim_force.variable_c",
    "gene.parametric.swim_force.signal_channel",
    "gene.parametric.swim_force.formula_type",
    "gene.parametric.density.variable_a",
    "gene.parametric.density.variable_b",
    "gene.parametric.density.variable_c",
    "gene.parametric.density.signal_channel",
    "gene.parametric.density.formula_type",
    "gene.parametric.myocyte_bending.variable_a",
    "gene.parametric.myocyte_bending.variable_b",
    "gene.parametric.myocyte_bending.variable_c",
    "gene.parametric.myocyte_bending.signal_channel",
    "gene.parametric.myocyte_bending.formula_type",
    "gene.parametric.myocyte_contraction.variable_a",
    "gene.parametric.myocyte_contraction.variable_b",
    "gene.parametric.myocyte_contraction.variable_c",
    "gene.parametric.myocyte_contraction.signal_channel",
    "gene.parametric.myocyte_contraction.formula_type",
    "gene.parametric.myocyte_lifting.variable_a",
    "gene.parametric.myocyte_lifting.variable_b",
    "gene.parametric.myocyte_lifting.variable_c",
    "gene.parametric.myocyte_lifting.signal_channel",
    "gene.parametric.myocyte_lifting.formula_type",
    "gene.parametric.stem_threshold_1.variable_a",
    "gene.parametric.stem_threshold_1.variable_b",
    "gene.parametric.stem_threshold_1.variable_c",
    "gene.parametric.stem_threshold_1.signal_channel",
    "gene.parametric.stem_threshold_1.formula_type",
    "gene.parametric.stem_threshold_2.variable_a",
    "gene.parametric.stem_threshold_2.variable_b",
    "gene.parametric.stem_threshold_2.variable_c",
    "gene.parametric.stem_threshold_2.signal_channel",
    "gene.parametric.stem_threshold_2.formula_type",
    "gene.parametric.signal_output_0.variable_a",
    "gene.parametric.signal_output_0.variable_b",
    "gene.parametric.signal_output_0.variable_c",
    "gene.parametric.signal_output_0.signal_channel",
    "gene.parametric.signal_output_0.formula_type",
    "gene.parametric.signal_output_1.variable_a",
    "gene.parametric.signal_output_1.variable_b",
    "gene.parametric.signal_output_1.variable_c",
    "gene.parametric.signal_output_1.signal_channel",
    "gene.parametric.signal_output_1.formula_type",
    "gene.parametric.signal_output_2.variable_a",
    "gene.parametric.signal_output_2.variable_b",
    "gene.parametric.signal_output_2.variable_c",
    "gene.parametric.signal_output_2.signal_channel",
    "gene.parametric.signal_output_2.formula_type",
    "gene.parametric.signal_output_3.variable_a",
    "gene.parametric.signal_output_3.variable_b",
    "gene.parametric.signal_output_3.variable_c",
    "gene.parametric.signal_output_3.signal_channel",
    "gene.parametric.signal_output_3.formula_type",
    "gene.integer.virus_from",
    "gene.integer.virus_to",
    "gene.integer.smell_out",
    "gene.integer.smell_type",
    "gene.integer.secrete_type",
    "gene.integer.gen_signal_id_0",
    "gene.integer.gen_signal_id_1",
    "gene.integer.gen_signal_id_2",
    "gene.integer.gen_signal_id_3",
    "gene.integer.max_connection_count",
    "gene.integer.telomerase_length",
    "gene.float.smell_amplitude",
    "gene.float.smell_color_hue",
    "gene.float.smell_color_saturation",
    "gene.float.smell_color_value",
    "gene.float.smell_threshold",
    "gene.float.adhesin_length",
    "gene.float.cytoskeleton",
    "gene.structural.duplicate_child_gene",
    "gene.structural.insert_linked_child_gene",
    "gene.structural.split_child_edge",
    "gene.structural.bypass_child_gene",
    "gene.structural.local_edge_retarget",
    "gene.structural.clone_parent_gene",
    "genome.length.add_gene",
    "genome.length.delete_gene"
)
