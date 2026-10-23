#pragma once

#include <cmath>

class CellDataConstants {
public:
    static constexpr int BOUNDARY_CONNECTION_INDEX = -1;

    static constexpr double MASS_PER_AREA = 400.0;
    static constexpr double RADIUS_CHANGE_SPEED = 10.0;
    static constexpr double MAX_CELL_RADIUS = 0.03;
    static constexpr double TURGOR_PRESSURE = 500.0 * MAX_CELL_RADIUS / 0.03;
    static constexpr double MIN_MASS = MAX_CELL_RADIUS * MAX_CELL_RADIUS * MASS_PER_AREA / 5.0;
    static constexpr double COLOR_CHANGE_SPEED = 8.0;
    static constexpr double MAX_MASS_FLOW = 0.1;
    static constexpr double MAX_NITRATE_FLOW = 0.6;
    static constexpr double ANGULAR_VELOCITY_CYTOPLASM_VISCOSITY = 1.0;
    static constexpr double MAX_MASS = MAX_CELL_RADIUS * MAX_CELL_RADIUS * MASS_PER_AREA;
    static constexpr float CYTOSKELETON_COST = (static_cast<float>(MAX_MASS) * 0.5f - static_cast<float>(MIN_MASS)) / 2.0f;
    static constexpr double NO_PRIORITY_MASS_MARGIN = MIN_MASS * 0.2;
    static constexpr double CORPSE_EFFICIENCY = 0.9;
    static constexpr double DEVOROCYTE_SUCK_SPEED = MIN_MASS * 10.0;
    static constexpr double MAX_EAT_SPEED = 1.0;
    inline static const double MIN_CELL_RADIUS = std::sqrt(MIN_MASS / MASS_PER_AREA);
    static constexpr double INITIAL_MASS = MIN_MASS * 4.0;
    static constexpr float SMELL_SENSITIVITY = 10.0f;
    static constexpr int MAX_NEIGHBORS = 20;
    static constexpr int MAX_GENES_COUNT = 40;
    static constexpr int MAX_GLUEOCYTE_LINK_COUNT = 5;
    static constexpr float VALUE_VARIABLE_MAX = 8.0f;
    static constexpr double MAX_LIPO_MASS = MAX_MASS * 6.0;
    static constexpr float MAX_LIPIDS = static_cast<float>(MAX_LIPO_MASS - MAX_MASS);
    static constexpr double AGE_OF_CONSENT = 0.5;
    static constexpr double HAIR_TAU = 100.0;
    static constexpr double MAX_AGE = 60.0 * 4.0;
    static constexpr double MAX_NITRATES = 1.0;
    static constexpr double MIN_NITRATES = 0.1;
    static constexpr double MIN_SECRETE_NITRATES = 2.0 * MIN_NITRATES;
    static constexpr double LINK_ATTACH_DEPTH = 0.4;
    static constexpr double LINK_SPRING_COEFFICIENT = 30.0;
    static constexpr double LINK_SUBSTRATE_DAMPING = 50.0 * MAX_CELL_RADIUS / 0.03;
    static constexpr double LINK_INTERCELL_DAMPING = 25.0;
    static constexpr double LINK_MARGIN = 2.0;
    static constexpr float MAX_LINK_STIFFNESS = 35.0f;
    static constexpr int SIGNAL_SUBSTANCE_COUNT = 4;
    static constexpr int AUX_SIGNAL_SUBSTANCE_COUNT = 4;
    static constexpr int NEUROCYTE_OUTPUT_COUNT = 4;
    static constexpr int MUTATE_PERIOD = 15;
    static constexpr int SIMULTANEOUS_MUTATIONS_COUNT = 3;
    static constexpr float TOXIN_SPEED = 60.0f;
    static constexpr float TOXICATION_SPEED = 0.05f;
    static constexpr float DEFENSIN_SPEED = 0.1f;
    static constexpr float TOXIN_DECAY = 0.05f;
    static constexpr float TOXIN_FLOW = 3.0f;
    static constexpr float MAX_SIGNAL_SUBSTANCE_OUTPUT = 20.0f;
    static constexpr float MAX_SIGNAL_SUBSTANCE_OUTPUT_INVERSE = 1.0f / MAX_SIGNAL_SUBSTANCE_OUTPUT;
    static constexpr float MAX_SWIM_FORCE = 1.0f;
    static constexpr float SIGNAL_SUBSTANCE_INITIAL = 0.01f;
    static constexpr float SIGNAL_SUBSTANCE_FLOW = 5.0f;
    static constexpr float SIGNAL_SUBSTANCE_DECAY = 5.0f;
    static constexpr float MYOCYTE_USE_COST = 0.00008f;
    static constexpr float MAX_MASS_FLOW_PRIORITY = 10.0f;
    static constexpr float MIN_MASS_FLOW_PRIORITY = 0.1f;
    static constexpr double PANIC_MASS = MIN_MASS * 1.3;
    static constexpr float MIN_RATIO = 0.1f;
    static constexpr float MAX_SPLIT_MASS = static_cast<float>(MAX_MASS + 0.1 * MIN_MASS);
    static constexpr float MIN_SPLIT_MASS = static_cast<float>(0.9 * MIN_MASS);

    static constexpr int MAX_COLLISIONS_PER_CELL = 32;
    // Every directed Cell contact can queue one ciliocyte interaction and a
    // Cell can additionally touch the dish boundary in the same update.
    static constexpr int MAX_CILIOCYTE_INTERACTIONS_PER_CELL =
            MAX_COLLISIONS_PER_CELL + 1;
};
