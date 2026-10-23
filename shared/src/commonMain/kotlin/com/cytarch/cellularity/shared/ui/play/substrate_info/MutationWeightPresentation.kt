package com.cytarch.cellularity.shared.ui.play.substrate_info

import androidx.compose.runtime.Composable
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_add_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_adhesin_length
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_bypass_child_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_cell_type
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_1_angle
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_1_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_1_keep_adhesin_connections
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_1_mirror
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_2_angle
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_2_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_2_keep_adhesin_connections
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_child_2_mirror
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_clone_parent_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_cell_color_hue
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_cell_color_saturation
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_cell_color_value
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_cytoskeleton
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_delete_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_duplicate_child_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_insert_linked_child_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_adhesin_stiffness
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_local_edge_retarget
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_produce_adhesin
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_mass_flow_priority
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_maximum_adhesin_connections
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_numbered_label_format
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_cell_density
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_formula_type
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_label_format
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_myocyte_bending
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_myocyte_contraction
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_myocyte_lifting
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_signal_channel
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_signal_output
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_stem_threshold_1
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_stem_threshold_2
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_swim_force
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_variable_a
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_variable_b
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_parametric_variable_c
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_secretion_type
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_signal_output_channel
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_amplitude
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_color_hue
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_color_saturation
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_color_value
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_output_channel
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_threshold
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_sense_type
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_split_angle
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_split_child_edge
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_split_mass
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_split_ratio
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_prioritize_survival
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_telomeres_after_fertilization
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_virus_source_gene
import com.cytarch.cellularity.shared.generated.resources.mutation_weight_virus_target_gene
import com.cytarch.cellularity.shared.model.EnvironmentContract
import org.jetbrains.compose.resources.StringResource
import org.jetbrains.compose.resources.stringResource

/** Presentation metadata layered over the native mutation-weight index contract. */
internal data class MutationWeightDescriptor(
    val index: Int,
    val stableId: String,
    val label: MutationWeightLabelDescriptor
)

internal sealed interface MutationWeightLabelDescriptor {
    data class Direct(val resource: StringResource) : MutationWeightLabelDescriptor

    data class Numbered(
        val resource: StringResource,
        val displayNumber: Int
    ) : MutationWeightLabelDescriptor

    data class Parametric(
        val family: MutationParametricFamilyDescriptor,
        val suffix: MutationParametricSuffixDescriptor
    ) : MutationWeightLabelDescriptor
}

internal data class MutationParametricFamilyDescriptor(
    val stableSegment: String,
    val resource: StringResource,
    val displayNumber: Int? = null
)

internal data class MutationParametricSuffixDescriptor(
    val stableSegment: String,
    val resource: StringResource
)

internal val mutationParametricFamilyDescriptors: List<MutationParametricFamilyDescriptor> =
    listOf(
        MutationParametricFamilyDescriptor(
            "swim_force",
            Res.string.mutation_weight_parametric_swim_force
        ),
        MutationParametricFamilyDescriptor(
            "density",
            Res.string.mutation_weight_parametric_cell_density
        ),
        MutationParametricFamilyDescriptor(
            "myocyte_bending",
            Res.string.mutation_weight_parametric_myocyte_bending
        ),
        MutationParametricFamilyDescriptor(
            "myocyte_contraction",
            Res.string.mutation_weight_parametric_myocyte_contraction
        ),
        MutationParametricFamilyDescriptor(
            "myocyte_lifting",
            Res.string.mutation_weight_parametric_myocyte_lifting
        ),
        MutationParametricFamilyDescriptor(
            "stem_threshold_1",
            Res.string.mutation_weight_parametric_stem_threshold_1
        ),
        MutationParametricFamilyDescriptor(
            "stem_threshold_2",
            Res.string.mutation_weight_parametric_stem_threshold_2
        ),
        MutationParametricFamilyDescriptor(
            "signal_output_0",
            Res.string.mutation_weight_parametric_signal_output,
            displayNumber = 1
        ),
        MutationParametricFamilyDescriptor(
            "signal_output_1",
            Res.string.mutation_weight_parametric_signal_output,
            displayNumber = 2
        ),
        MutationParametricFamilyDescriptor(
            "signal_output_2",
            Res.string.mutation_weight_parametric_signal_output,
            displayNumber = 3
        ),
        MutationParametricFamilyDescriptor(
            "signal_output_3",
            Res.string.mutation_weight_parametric_signal_output,
            displayNumber = 4
        )
    )

internal val mutationParametricSuffixDescriptors: List<MutationParametricSuffixDescriptor> =
    listOf(
        MutationParametricSuffixDescriptor(
            "variable_a",
            Res.string.mutation_weight_parametric_variable_a
        ),
        MutationParametricSuffixDescriptor(
            "variable_b",
            Res.string.mutation_weight_parametric_variable_b
        ),
        MutationParametricSuffixDescriptor(
            "variable_c",
            Res.string.mutation_weight_parametric_variable_c
        ),
        MutationParametricSuffixDescriptor(
            "signal_channel",
            Res.string.mutation_weight_parametric_signal_channel
        ),
        MutationParametricSuffixDescriptor(
            "formula_type",
            Res.string.mutation_weight_parametric_formula_type
        )
    )

internal val mutationWeightDescriptors: List<MutationWeightDescriptor> = buildList {
    fun direct(stableId: String, resource: StringResource) {
        add(MutationWeightDescriptor(size, stableId, MutationWeightLabelDescriptor.Direct(resource)))
    }

    fun numbered(stableId: String, resource: StringResource, displayNumber: Int) {
        add(
            MutationWeightDescriptor(
                size,
                stableId,
                MutationWeightLabelDescriptor.Numbered(resource, displayNumber)
            )
        )
    }

    direct("gene.color.hue", Res.string.mutation_weight_cell_color_hue)
    direct("gene.color.saturation", Res.string.mutation_weight_cell_color_saturation)
    direct("gene.color.value", Res.string.mutation_weight_cell_color_value)

    direct("gene.continuous.split_angle", Res.string.mutation_weight_split_angle)
    direct("gene.continuous.child1_angle", Res.string.mutation_weight_child_1_angle)
    direct("gene.continuous.child2_angle", Res.string.mutation_weight_child_2_angle)
    direct(
        "gene.continuous.mass_flow_priority",
        Res.string.mutation_weight_mass_flow_priority
    )
    direct("gene.continuous.split_mass", Res.string.mutation_weight_split_mass)
    direct("gene.continuous.split_ratio", Res.string.mutation_weight_split_ratio)
    direct("gene.continuous.link_strength", Res.string.mutation_weight_adhesin_stiffness)

    direct("gene.child_index.child1", Res.string.mutation_weight_child_1_gene)
    direct("gene.child_index.child2", Res.string.mutation_weight_child_2_gene)

    direct("gene.boolean.make_link", Res.string.mutation_weight_produce_adhesin)
    direct("gene.boolean.child1_keep_links", Res.string.mutation_weight_child_1_keep_adhesin_connections)
    direct("gene.boolean.child2_keep_links", Res.string.mutation_weight_child_2_keep_adhesin_connections)
    direct("gene.boolean.stay_alive", Res.string.mutation_weight_prioritize_survival)
    direct("gene.boolean.child1_mirror", Res.string.mutation_weight_child_1_mirror)
    direct("gene.boolean.child2_mirror", Res.string.mutation_weight_child_2_mirror)

    direct("gene.cell_type", Res.string.mutation_weight_cell_type)

    mutationParametricFamilyDescriptors.forEach { family ->
        mutationParametricSuffixDescriptors.forEach { suffix ->
            add(
                MutationWeightDescriptor(
                    index = size,
                    stableId = "gene.parametric.${family.stableSegment}.${suffix.stableSegment}",
                    label = MutationWeightLabelDescriptor.Parametric(family, suffix)
                )
            )
        }
    }

    direct("gene.integer.virus_from", Res.string.mutation_weight_virus_source_gene)
    direct("gene.integer.virus_to", Res.string.mutation_weight_virus_target_gene)
    direct("gene.integer.smell_out", Res.string.mutation_weight_sense_output_channel)
    direct("gene.integer.smell_type", Res.string.mutation_weight_sense_type)
    direct("gene.integer.secrete_type", Res.string.mutation_weight_secretion_type)
    repeat(4) { outputIndex ->
        numbered(
            stableId = "gene.integer.gen_signal_id_$outputIndex",
            resource = Res.string.mutation_weight_signal_output_channel,
            displayNumber = outputIndex + 1
        )
    }
    direct(
        "gene.integer.max_connection_count",
        Res.string.mutation_weight_maximum_adhesin_connections
    )
    direct("gene.integer.telomerase_length", Res.string.mutation_weight_telomeres_after_fertilization)

    direct("gene.float.smell_amplitude", Res.string.mutation_weight_sense_amplitude)
    direct("gene.float.smell_color_hue", Res.string.mutation_weight_sense_color_hue)
    direct(
        "gene.float.smell_color_saturation",
        Res.string.mutation_weight_sense_color_saturation
    )
    direct("gene.float.smell_color_value", Res.string.mutation_weight_sense_color_value)
    direct("gene.float.smell_threshold", Res.string.mutation_weight_sense_threshold)
    direct("gene.float.adhesin_length", Res.string.mutation_weight_adhesin_length)
    direct("gene.float.cytoskeleton", Res.string.mutation_weight_cytoskeleton)

    direct(
        "gene.structural.duplicate_child_gene",
        Res.string.mutation_weight_duplicate_child_gene
    )
    direct(
        "gene.structural.insert_linked_child_gene",
        Res.string.mutation_weight_insert_linked_child_gene
    )
    direct("gene.structural.split_child_edge", Res.string.mutation_weight_split_child_edge)
    direct("gene.structural.bypass_child_gene", Res.string.mutation_weight_bypass_child_gene)
    direct("gene.structural.local_edge_retarget", Res.string.mutation_weight_local_edge_retarget)
    direct("gene.structural.clone_parent_gene", Res.string.mutation_weight_clone_parent_gene)

    direct("genome.length.add_gene", Res.string.mutation_weight_add_gene)
    direct("genome.length.delete_gene", Res.string.mutation_weight_delete_gene)

}.also { descriptors ->
    check(descriptors.size == EnvironmentContract.MUTATION_WEIGHT_COUNT) {
        "Mutation weight descriptors do not match the environment contract"
    }
    check(descriptors.map(MutationWeightDescriptor::index) == descriptors.indices.toList()) {
        "Mutation weight descriptor indices are not contiguous"
    }
    check(descriptors.map(MutationWeightDescriptor::stableId).distinct().size == descriptors.size) {
        "Mutation weight stable IDs must be unique"
    }
}

@Composable
internal fun mutationWeightLabel(descriptor: MutationWeightDescriptor): String =
    when (val label = descriptor.label) {
        is MutationWeightLabelDescriptor.Direct -> stringResource(label.resource)
        is MutationWeightLabelDescriptor.Numbered -> stringResource(
            Res.string.mutation_weight_numbered_label_format,
            stringResource(label.resource),
            label.displayNumber
        )
        is MutationWeightLabelDescriptor.Parametric -> stringResource(
            Res.string.mutation_weight_parametric_label_format,
            mutationParametricFamilyLabel(label.family),
            stringResource(label.suffix.resource)
        )
    }

@Composable
private fun mutationParametricFamilyLabel(
    descriptor: MutationParametricFamilyDescriptor
): String {
    val baseLabel = stringResource(descriptor.resource)
    val displayNumber = descriptor.displayNumber ?: return baseLabel
    return stringResource(
        Res.string.mutation_weight_numbered_label_format,
        baseLabel,
        displayNumber
    )
}

internal fun MutationWeightDescriptor.matchesFilter(
    localizedLabel: String,
    filter: String
): Boolean {
    val normalizedFilter = filter.trim().lowercase()
    return normalizedFilter.isEmpty() ||
        stableId.lowercase().contains(normalizedFilter) ||
        localizedLabel.lowercase().contains(normalizedFilter)
}

internal fun MutationWeightLabelDescriptor.referencedResources(): List<StringResource> =
    when (this) {
        is MutationWeightLabelDescriptor.Direct -> listOf(resource)
        is MutationWeightLabelDescriptor.Numbered -> listOf(
            resource,
            Res.string.mutation_weight_numbered_label_format
        )
        is MutationWeightLabelDescriptor.Parametric -> buildList {
            add(family.resource)
            add(suffix.resource)
            add(Res.string.mutation_weight_parametric_label_format)
            if (family.displayNumber != null) {
                add(Res.string.mutation_weight_numbered_label_format)
            }
        }
    }
