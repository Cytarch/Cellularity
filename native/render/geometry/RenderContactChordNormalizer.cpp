#include "render/geometry/RenderContactChordNormalizer.h"

#include <cassert>
#include <chrono>

#include "render/frame/RenderContactLimits.h"

namespace cellularity::render {
namespace {

#ifdef CELLULARITY_RENDER_PROFILE
#define CELLULARITY_PROFILE_IF(profile_ptr) if ((profile_ptr) != nullptr)
#endif

int compareForLegacyAngularOrder(const RenderContactChord& chord,
        const RenderContactChord& other) {
    if (chord.isDiscarded && other.isDiscarded) {
        return 0;
    }

    if (chord.isDiscarded) {
        return 1;
    }

    if (other.isDiscarded) {
        return -1;
    }

    if (other.y1 * chord.y1 < 0.0f) {
        if (chord.y1 > 0.0f) {
            return -1;
        }
        return 1;
    }

    if (other.y1 * chord.x1 > chord.y1 * other.x1) {
        return -1;
    }

    return 1;
}

} // namespace

void clipAndOrderRenderContactChords(
        RenderContactChord* contactChords, int contactChordCount,
        RenderContactChordNormalizationProfile* profile) {
    (void)profile;
    assert(contactChordCount >= 0);
    assert(contactChordCount <= kMaximumRenderContactChordsPerCell);
    assert(contactChordCount == 0 || contactChords != nullptr);
#ifdef CELLULARITY_RENDER_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->calls++;
        profile->contactChordCountSum += contactChordCount;
    }
#endif
    if (contactChordCount <= 1) {
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->skippedCalls++;
        }
#endif
        return;
    }

#ifdef CELLULARITY_RENDER_PROFILE
    auto firstPassStart = std::chrono::steady_clock::now();
#endif
    auto processFirstPassPair = [&](int otherChordIndex,
                                        int candidateChordIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->firstPassPairs++;
        }
#endif
        RenderContactChord& candidateChord = contactChords[candidateChordIndex];
        if (candidateChord.isDiscarded) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->firstPassDiscardedSkips++;
            }
#endif
            return;
        }

        RenderContactChord& otherChord = contactChords[otherChordIndex];

        double firstDirectionX = otherChord.x2 - otherChord.x1;
        double secondDirectionX = candidateChord.x1 - candidateChord.x2;
        double firstDirectionY = otherChord.y2 - otherChord.y1;
        double secondDirectionY = candidateChord.y1 - candidateChord.y2;

        double rightHandX = candidateChord.x1 - otherChord.x1;
        double rightHandY = candidateChord.y1 - otherChord.y1;

        double determinant =
                firstDirectionX * secondDirectionY - secondDirectionX * firstDirectionY;
        double firstParameter =
                (secondDirectionY * rightHandX - secondDirectionX * rightHandY) / determinant;
        double secondParameter =
                (-firstDirectionY * rightHandX + firstDirectionX * rightHandY) / determinant;

        double orientation =
                (otherChord.x2 - candidateChord.x1) * secondDirectionY - (otherChord.y2 - candidateChord.y1) * secondDirectionX;

        if (firstParameter <= 0.0 || firstParameter >= 1.0 || secondParameter <= 0.0 || secondParameter >= 1.0) {
            return;
        }

#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->firstPassIntersections++;
            profile->firstPassJoinMarks++;
        }
#endif
        double intersectionX = otherChord.x1 + firstParameter * firstDirectionX;
        double intersectionY = otherChord.y1 + firstParameter * firstDirectionY;

        if (orientation < 0.0f) {
            candidateChord.x1 = otherChord.x2 = intersectionX;
            candidateChord.y1 = otherChord.y2 = intersectionY;
            otherChord.joinsNextChord = true;
        } else {
            candidateChord.x2 = otherChord.x1 = intersectionX;
            candidateChord.y2 = otherChord.y1 = intersectionY;
            candidateChord.joinsNextChord = true;
        }

        if (otherChord.x1 * otherChord.y2 - otherChord.y1 * otherChord.x2 < 0.0) {
            otherChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->firstPassDiscardMarks++;
            }
#endif
        }

        if (candidateChord.x1 * candidateChord.y2 - candidateChord.y1 * candidateChord.x2 < 0.0) {
            candidateChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->firstPassDiscardMarks++;
            }
#endif
        }
    };

    auto processSecondPassPair = [&](int candidateChordIndex,
                                         int otherChordIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->secondPassPairs++;
        }
#endif
        if (otherChordIndex == candidateChordIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->secondPassSelfSkips++;
            }
#endif
            return false;
        }

        RenderContactChord& candidateChord = contactChords[candidateChordIndex];
        RenderContactChord& otherChord = contactChords[otherChordIndex];

        if (candidateChord.x1 == otherChord.x2 && candidateChord.y1 == otherChord.y2) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->secondPassEndpointSkips++;
            }
#endif
            return false;
        }

        if (candidateChord.x2 == otherChord.x1 && candidateChord.y2 == otherChord.y1) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->secondPassEndpointSkips++;
            }
#endif
            return false;
        }

        double firstDirectionX = candidateChord.x1;
        double secondDirectionX = otherChord.x1 - otherChord.x2;
        double firstDirectionY = candidateChord.y1;
        double secondDirectionY = otherChord.y1 - otherChord.y2;

        double rightHandX = otherChord.x1;
        double rightHandY = otherChord.y1;

        double determinant =
                firstDirectionX * secondDirectionY - secondDirectionX * firstDirectionY;
        double firstParameter =
                (secondDirectionY * rightHandX - secondDirectionX * rightHandY) / determinant;

        if (firstParameter > 0.0 && firstParameter < 1.0) {
            candidateChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->secondPassDiscardMarks++;
            }
#endif
            return true;
        }
        return false;
    };

    if (contactChordCount >= 2 && contactChordCount <= 5) {
        processFirstPassPair(0, 1);
        if (contactChordCount == 2) {
            processFirstPassPair(1, 0);
        } else if (contactChordCount == 3) {
            processFirstPassPair(0, 2);
            processFirstPassPair(1, 2);
            processFirstPassPair(1, 0);
            processFirstPassPair(2, 0);
            processFirstPassPair(2, 1);
        } else if (contactChordCount == 4) {
            processFirstPassPair(0, 2);
            processFirstPassPair(0, 3);
            processFirstPassPair(1, 2);
            processFirstPassPair(1, 3);
            processFirstPassPair(1, 0);
            processFirstPassPair(2, 3);
            processFirstPassPair(2, 0);
            processFirstPassPair(2, 1);
            processFirstPassPair(3, 0);
            processFirstPassPair(3, 1);
            processFirstPassPair(3, 2);
        } else {
            processFirstPassPair(0, 2);
            processFirstPassPair(0, 3);
            processFirstPassPair(0, 4);
            processFirstPassPair(1, 2);
            processFirstPassPair(1, 3);
            processFirstPassPair(1, 4);
            processFirstPassPair(1, 0);
            processFirstPassPair(2, 3);
            processFirstPassPair(2, 4);
            processFirstPassPair(2, 0);
            processFirstPassPair(2, 1);
            processFirstPassPair(3, 4);
            processFirstPassPair(3, 0);
            processFirstPassPair(3, 1);
            processFirstPassPair(3, 2);
            processFirstPassPair(4, 0);
            processFirstPassPair(4, 1);
            processFirstPassPair(4, 2);
            processFirstPassPair(4, 3);
        }
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->firstPassNanos +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - firstPassStart)
                            .count();
        }

        auto secondPassStart = std::chrono::steady_clock::now();
#endif
        auto processSecondPassForChord = [&](int candidateChordIndex) {
            if (contactChords[candidateChordIndex].isDiscarded) {
                return;
            }
            for (int otherChordIndex = 0; otherChordIndex < contactChordCount;
                    otherChordIndex++) {
                if (processSecondPassPair(candidateChordIndex, otherChordIndex)) {
                    break;
                }
            }
        };
        processSecondPassForChord(0);
        processSecondPassForChord(1);
        if (contactChordCount >= 3) {
            processSecondPassForChord(2);
        }
        if (contactChordCount >= 4) {
            processSecondPassForChord(3);
        }
        if (contactChordCount == 5) {
            processSecondPassForChord(4);
        }
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->secondPassNanos +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - secondPassStart)
                            .count();
        }

        auto sortStart = std::chrono::steady_clock::now();
#endif
        if (contactChordCount == 2) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->sortComparisons++;
            }
#endif
            if (compareForLegacyAngularOrder(contactChords[0], contactChords[1]) > 0) {
                RenderContactChord temporaryChord = contactChords[0];
                contactChords[0] = contactChords[1];
                contactChords[1] = temporaryChord;
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->sortMoves += 3;
                }
#endif
            }
        } else {
            for (int chordIndex = 1; chordIndex < contactChordCount; chordIndex++) {
                RenderContactChord keyChord = contactChords[chordIndex];
                int insertionIndex = chordIndex - 1;
                while (insertionIndex >= 0
#ifdef CELLULARITY_RENDER_PROFILE
                        && (profile == nullptr || (++profile->sortComparisons, true))
#endif
                        && compareForLegacyAngularOrder(contactChords[insertionIndex],
                                   keyChord)
                                > 0) {
                    contactChords[insertionIndex + 1] = contactChords[insertionIndex];
#ifdef CELLULARITY_RENDER_PROFILE
                    CELLULARITY_PROFILE_IF(profile) {
                        profile->sortMoves++;
                    }
#endif
                    insertionIndex--;
                }
                contactChords[insertionIndex + 1] = keyChord;
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->sortMoves++;
                }
#endif
            }
        }
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->sortNanos +=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - sortStart)
                            .count();
        }
#endif
        return;
    }

    for (int otherChordIndex = 0; otherChordIndex < contactChordCount;
            otherChordIndex++) {
        for (int chordOffset = 1; chordOffset < contactChordCount; chordOffset++) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->firstPassPairs++;
            }
#endif
            int candidateChordIndex = otherChordIndex + chordOffset;
            if (candidateChordIndex >= contactChordCount) {
                candidateChordIndex -= contactChordCount;
            }

            RenderContactChord& candidateChord = contactChords[candidateChordIndex];
            if (candidateChord.isDiscarded) {
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->firstPassDiscardedSkips++;
                }
#endif
                continue;
            }

            RenderContactChord& otherChord = contactChords[otherChordIndex];

            double firstDirectionX = otherChord.x2 - otherChord.x1;
            double secondDirectionX = candidateChord.x1 - candidateChord.x2;
            double firstDirectionY = otherChord.y2 - otherChord.y1;
            double secondDirectionY = candidateChord.y1 - candidateChord.y2;

            double rightHandX = candidateChord.x1 - otherChord.x1;
            double rightHandY = candidateChord.y1 - otherChord.y1;

            double determinant = firstDirectionX * secondDirectionY - secondDirectionX * firstDirectionY;
            double firstParameter =
                    (secondDirectionY * rightHandX - secondDirectionX * rightHandY) / determinant;
            double secondParameter =
                    (-firstDirectionY * rightHandX + firstDirectionX * rightHandY) / determinant;

            double orientation =
                    (otherChord.x2 - candidateChord.x1) * secondDirectionY - (otherChord.y2 - candidateChord.y1) * secondDirectionX;

            if (firstParameter <= 0.0 || firstParameter >= 1.0 || secondParameter <= 0.0 || secondParameter >= 1.0) {
                continue;
            }

#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->firstPassIntersections++;
                profile->firstPassJoinMarks++;
            }
#endif
            double intersectionX = otherChord.x1 + firstParameter * firstDirectionX;
            double intersectionY = otherChord.y1 + firstParameter * firstDirectionY;

            if (orientation < 0.0f) {
                candidateChord.x1 = otherChord.x2 = intersectionX;
                candidateChord.y1 = otherChord.y2 = intersectionY;
                otherChord.joinsNextChord = true;
            } else {
                candidateChord.x2 = otherChord.x1 = intersectionX;
                candidateChord.y2 = otherChord.y1 = intersectionY;
                candidateChord.joinsNextChord = true;
            }

            if (otherChord.x1 * otherChord.y2 - otherChord.y1 * otherChord.x2 < 0.0) {
                otherChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->firstPassDiscardMarks++;
                }
#endif
            }

            if (candidateChord.x1 * candidateChord.y2 - candidateChord.y1 * candidateChord.x2 < 0.0) {
                candidateChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->firstPassDiscardMarks++;
                }
#endif
            }
        }
    }
#ifdef CELLULARITY_RENDER_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->firstPassNanos +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - firstPassStart)
                        .count();
    }

    auto secondPassStart = std::chrono::steady_clock::now();
#endif
    for (int candidateChordIndex = 0; candidateChordIndex < contactChordCount;
            candidateChordIndex++) {
        RenderContactChord& candidateChord = contactChords[candidateChordIndex];

        if (candidateChord.isDiscarded) {
            continue;
        }

        for (int otherChordIndex = 0; otherChordIndex < contactChordCount;
                otherChordIndex++) {
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->secondPassPairs++;
            }
#endif
            if (otherChordIndex == candidateChordIndex) {
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->secondPassSelfSkips++;
                }
#endif
                continue;
            }

            RenderContactChord& otherChord = contactChords[otherChordIndex];

            if (candidateChord.x1 == otherChord.x2 && candidateChord.y1 == otherChord.y2) {
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->secondPassEndpointSkips++;
                }
#endif
                continue;
            }

            if (candidateChord.x2 == otherChord.x1 && candidateChord.y2 == otherChord.y1) {
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->secondPassEndpointSkips++;
                }
#endif
                continue;
            }

            double firstDirectionX = candidateChord.x1;
            double secondDirectionX = otherChord.x1 - otherChord.x2;
            double firstDirectionY = candidateChord.y1;
            double secondDirectionY = otherChord.y1 - otherChord.y2;

            double rightHandX = otherChord.x1;
            double rightHandY = otherChord.y1;

            double determinant = firstDirectionX * secondDirectionY - secondDirectionX * firstDirectionY;
            double firstParameter =
                    (secondDirectionY * rightHandX - secondDirectionX * rightHandY) / determinant;

            if (firstParameter > 0.0 && firstParameter < 1.0) {
                candidateChord.isDiscarded = true;
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->secondPassDiscardMarks++;
                }
#endif
                break;
            }
        }
    }
#ifdef CELLULARITY_RENDER_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->secondPassNanos +=
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - secondPassStart)
                        .count();
    }

    auto sortStart = std::chrono::steady_clock::now();
#endif
    if (contactChordCount == 2) {
#ifdef CELLULARITY_RENDER_PROFILE
        CELLULARITY_PROFILE_IF(profile) {
            profile->sortComparisons++;
        }
#endif
        if (compareForLegacyAngularOrder(contactChords[0], contactChords[1]) > 0) {
            RenderContactChord temporaryChord = contactChords[0];
            contactChords[0] = contactChords[1];
            contactChords[1] = temporaryChord;
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->sortMoves += 3;
            }
#endif
        }
    } else {
        for (int chordIndex = 1; chordIndex < contactChordCount; chordIndex++) {
            RenderContactChord keyChord = contactChords[chordIndex];
            int insertionIndex = chordIndex - 1;
            while (insertionIndex >= 0
#ifdef CELLULARITY_RENDER_PROFILE
                    && (profile == nullptr || (++profile->sortComparisons, true))
#endif
                    && compareForLegacyAngularOrder(contactChords[insertionIndex],
                               keyChord)
                            > 0) {
                contactChords[insertionIndex + 1] = contactChords[insertionIndex];
#ifdef CELLULARITY_RENDER_PROFILE
                CELLULARITY_PROFILE_IF(profile) {
                    profile->sortMoves++;
                }
#endif
                insertionIndex--;
            }
            contactChords[insertionIndex + 1] = keyChord;
#ifdef CELLULARITY_RENDER_PROFILE
            CELLULARITY_PROFILE_IF(profile) {
                profile->sortMoves++;
            }
#endif
        }
    }
#ifdef CELLULARITY_RENDER_PROFILE
    CELLULARITY_PROFILE_IF(profile) {
        profile->sortNanos += std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - sortStart)
                                      .count();
    }
#endif
}

#ifdef CELLULARITY_RENDER_PROFILE
#undef CELLULARITY_PROFILE_IF
#endif

} // namespace cellularity::render
