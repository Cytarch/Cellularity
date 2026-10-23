#!/usr/bin/env python3
"""Exercise Link architecture checks without modifying native source files."""

from __future__ import annotations

import contextlib
import importlib.util
import io
import re
import unittest
from pathlib import Path
from unittest.mock import patch


REPO_ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    "native_session_boundary", REPO_ROOT / "scripts/verify-native-session-boundary.py"
)
assert spec is not None and spec.loader is not None
boundary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(boundary)


class NativeSessionBoundaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.paths = (
            boundary.RENDER_FRAME_BUILDER_SOURCE,
            boundary.RENDER_LINK_GEOMETRY_BUILDER_HEADER,
            boundary.RENDER_LINK_GEOMETRY_BUILDER_SOURCE,
            boundary.RENDER_SOURCE_FRAME_HEADER,
            boundary.RENDER_SOURCE_CAPTURE_SOURCE,
        )
        cls.sources = tuple(path.read_text(encoding="utf-8") for path in cls.paths)
        cls.cmake = boundary.CMAKE_PATH.read_text(encoding="utf-8")

    def replace_source(self, index: int, old: str, new: str, count: int = 1) -> tuple[str, ...]:
        self.assertIn(old, self.sources[index], "Mutation must change actual current code")
        sources = list(self.sources)
        sources[index] = sources[index].replace(old, new, count)
        self.assertNotEqual(self.sources[index], sources[index])
        return tuple(sources)

    def assert_rejected(self, sources: tuple[str, ...], diagnostic: str) -> None:
        errors = boundary.verify_link_preparation_contract(*sources)
        self.assertTrue(any(diagnostic in error for error in errors), errors)

    def fixture_inventory(self, source: str, cmake: str | None = None) -> list[str]:
        return boundary.verify_link_case_inventory(
            source, self.cmake if cmake is None else cmake,
            "cellularity_link_render_contract_tests", "link_case",
            "tests/link_render_contract_tests.cpp",
        )

    def test_current_contract_and_all_cli_inventories(self) -> None:
        self.assertEqual([], boundary.verify_link_preparation_contract(*self.sources))
        for path, target, variable, source in (
            (boundary.LINK_RENDER_CONTRACT_TEST, "cellularity_link_render_contract_tests", "link_case", "tests/link_render_contract_tests.cpp"),
            (boundary.BOUNDARY_LINK_CAPTURE_TEST, "cellularity_boundary_link_capture_tests", "boundary_case", "tests/boundary_link_capture_tests.cpp"),
            (boundary.LINK_GEOMETRY_BUILDER_TEST, "cellularity_link_geometry_builder_tests", "geometry_case", "tests/link_geometry_builder_tests.cpp"),
        ):
            with self.subTest(target=target):
                self.assertEqual([], boundary.verify_link_case_inventory(
                    path.read_text(encoding="utf-8"), self.cmake, target, variable, source
                ))

    def test_resolver_parameter_and_both_build_calls_are_required(self) -> None:
        self.assert_rejected(self.replace_source(
            1, "RenderContactChordResolver& contactChordResolver", "RenderContactChordResolver& unusedResolver"
        ), "must receive the shared")
        call = "mLinkGeometryBuilder.build(source, frame, mContactChordResolver,"
        for method, occurrence in (("build", 0), ("profileBuild", 1)):
            with self.subTest(method=method):
                source = self.sources[0]
                positions = [match.start() for match in re.finditer(re.escape(call), source)]
                self.assertEqual(2, len(positions))
                position = positions[occurrence]
                changed = source[:position] + source[position:].replace(call, call.replace("mContactChordResolver", "otherResolver"), 1)
                self.assert_rejected((changed, *self.sources[1:]), f"::{method} must forward")

    def test_lookup_owner_neighbor_consumer_and_profiles_are_required(self) -> None:
        for old, new in (
            ("&contactChordProfile, normalizationProfile, &linkProfile", "nullptr, normalizationProfile, &linkProfile"),
            ("&contactChordProfile, normalizationProfile, &linkProfile", "&contactChordProfile, nullptr, &linkProfile"),
            ("&contactChordProfile, normalizationProfile, &linkProfile", "&contactChordProfile, normalizationProfile, nullptr"),
        ):
            with self.subTest(mutation=new):
                self.assert_rejected(self.replace_source(0, old, new), "must forward Link and chord profiling")
        for old, new, diagnostic in (
            ("source, link.sourceCellARecordIndex,", "source, 0,", "captured owner"),
            ("RenderContactChordResolver::Consumer::LinkLookup", "RenderContactChordResolver::Consumer::CellGeometry", "captured owner"),
            ("contactChordProfile, normalizationProfile);", "nullptr, nullptr);", "both profiles"),
            ("chords.data[index].neighborCellIndex == link.sourceCellBIndex", "chords.data[index].neighborCellIndex == 0", "captured neighbor"),
        ):
            with self.subTest(mutation=new):
                self.assert_rejected(self.replace_source(2, old, new), diagnostic)

    def test_missing_contact_cannot_exit_early(self) -> None:
        anchor = "if (matchingChord != nullptr && matchingChord->isDiscarded) {"
        for early_exit in (
            "if (matchingChord == nullptr) { return; }",
            "if (! matchingChord) return;",
            "if (chords.count == 0) { return; }",
            "if (chords.count <= 0) throw std::logic_error(\"missing contact\");",
        ):
            with self.subTest(early_exit=early_exit):
                self.assert_rejected(self.replace_source(2, anchor, early_exit + "\n        " + anchor), "must not discard or reject")
        self.assert_rejected(self.replace_source(2, "bool useContact = false;", "bool useContact = true;"), "retain normal and boundary fallback")
        self.assert_rejected(self.replace_source(2, "if (!useContact) {", "if (useContact) {"), "retain normal and boundary fallback")

    def test_discarded_contact_cannot_fall_back_or_append(self) -> None:
        discarded = boundary.cpp_block(
            boundary.cpp_code(self.sources[2]),
            r"\bif\s*\(\s*matchingChord\s*!=\s*nullptr\s*&&\s*matchingChord->isDiscarded\s*\)",
        )
        self.assertIsNotNone(discarded)
        assert discarded is not None
        original = self.sources[2][discarded[0]:discarded[1]]
        for replacement in (
            original.replace("return;", "matchingChord = nullptr;"),
            original.replace("return;", "ensureLinkAppendCapacity(); return;"),
            original.replace("matchingChord != nullptr", "matchingChord == nullptr"),
        ):
            with self.subTest(replacement=replacement):
                self.assert_rejected(self.replace_source(2, original, replacement), "Discarded contact")
        fallback = boundary.cpp_block(boundary.cpp_code(self.sources[2]), r"\bif\s*\(\s*!useContact\s*\)")
        self.assertIsNotNone(fallback)
        assert fallback is not None
        source = self.sources[2]
        changed = source[:discarded[0]] + source[discarded[1]:fallback[1]] + original + source[fallback[1]:]
        self.assert_rejected((*self.sources[:2], changed, *self.sources[3:]), "handled before the fallback")

    def test_boundary_sentinels_owner_payload_and_independent_capture_are_required(self) -> None:
        for old, new in (
            ("link.sourceCellBIndex = CellData::BOUNDARY_CONNECTION_INDEX;", "link.sourceCellBIndex = 0;"),
            ("link.cellBRuntimeId = kBoundaryCellRuntimeId;", "link.cellBRuntimeId = 0;"),
            ("ensureSourceCell(world, owner, true, frame)", "ensureSourceCell(world, owner, false, frame)"),
        ):
            with self.subTest(mutation=new):
                self.assert_rejected(self.replace_source(4, old, new), "sentinel/owner payload")
        self.assert_rejected(self.replace_source(
            3, "return cellBRuntimeId == kBoundaryCellRuntimeId;", "return cellBRuntimeId == kInvalidCellRuntimeId;"
        ), "stable boundary sentinel")
        for old, new in (
            ("captureBoundaryCandidateLinks(world, captureBounds, frame, profile);", ""),
            ("captureNormalCandidateLinks(world, captureBounds, frame, profile);", "if (world.mCellData.mTopology.edgeCount() == 0) { return; }\n    captureNormalCandidateLinks(world, captureBounds, frame, profile);"),
        ):
            with self.subTest(mutation=new):
                self.assert_rejected(self.replace_source(4, old, new), "independently of normal edge count")

    def test_comments_and_literals_cannot_fake_the_contract(self) -> None:
        sources = self.replace_source(2, "contactChordProfile, normalizationProfile);", "nullptr, nullptr);")
        fake = "contactChordResolver.resolve(source, link.sourceCellARecordIndex, RenderContactChordResolver::Consumer::LinkLookup, contactChordProfile, normalizationProfile);"
        for decoration in ("// " + fake, "/* " + fake + " */", 'R"check(' + fake + ')check";'):
            with self.subTest(decoration=decoration):
                changed = sources[2] + "\n" + decoration
                self.assert_rejected((*sources[:2], changed, *sources[3:]), "both profiles")
        reformatted = tuple(re.sub(r"\s+", " ", boundary.cpp_code(source)) for source in self.sources)
        self.assertEqual([], boundary.verify_link_preparation_contract(*reformatted))

    def test_cli_table_and_ctest_registration_cannot_drift(self) -> None:
        source = boundary.LINK_RENDER_CONTRACT_TEST.read_text(encoding="utf-8")
        self.assertEqual([], self.fixture_inventory(source))
        row = '{"no_contact_normal_link", testNoContactNormalLink},'
        self.assertIn(row, source)
        for changed in (
            source.replace(row, "// " + row),
            source.replace(row, row.replace("no_contact_normal_link", "contact_chord_present")),
            source.replace("std::array<TestCase, 17>", "std::array<TestCase, 16>"),
            source.replace("void testNoContactNormalLink()", "void unusedNoContactNormalLink()"),
        ):
            with self.subTest(kind="CLI table"):
                self.assertTrue(self.fixture_inventory(changed))
        for old, new in (
            ("            no_contact_normal_link\n", ""),
            ("COMMAND cellularity_link_render_contract_tests ${link_case}", "COMMAND cellularity_link_render_contract_tests"),
            ("tests/link_render_contract_tests.cpp", "tests/unused_link_render_contract_tests.cpp"),
        ):
            with self.subTest(mutation=new):
                self.assertIn(old, self.cmake)
                self.assertTrue(self.fixture_inventory(source, self.cmake.replace(old, new, 1)))

    def test_full_verifier_uses_positive_link_checks(self) -> None:
        with contextlib.redirect_stdout(io.StringIO()):
            boundary.main()
        original_read_text = Path.read_text
        path = boundary.RENDER_LINK_GEOMETRY_BUILDER_SOURCE
        changed = self.replace_source(2, "bool useContact = false;", "bool useContact = true;")[2]

        def read_changed(file: Path, *args, **kwargs):
            return changed if file == path else original_read_text(file, *args, **kwargs)

        output = io.StringIO()
        with patch.object(Path, "read_text", read_changed), contextlib.redirect_stderr(output):
            with self.assertRaises(SystemExit) as error:
                boundary.main()
        self.assertEqual(1, error.exception.code)
        self.assertIn("Missing contact must retain normal and boundary fallback paths", output.getvalue())

    def test_native_build_list_cannot_skip_contract_targets(self) -> None:
        original_read_text = Path.read_text
        path = boundary.NATIVE_BUILD_SCRIPT
        source = path.read_text(encoding="utf-8")
        for target in (
            "cellularity_link_render_contract_tests",
            "cellularity_boundary_link_capture_tests",
            "cellularity_link_geometry_builder_tests",
        ):
            with self.subTest(target=target):
                row = "    " + target + "\n"
                self.assertIn(row, source)
                changed = source.replace(row, "", 1) + "\n# " + target

                def read_changed(file: Path, *args, **kwargs):
                    return changed if file == path else original_read_text(file, *args, **kwargs)

                output = io.StringIO()
                with patch.object(Path, "read_text", read_changed), contextlib.redirect_stderr(output):
                    with self.assertRaises(SystemExit) as error:
                        boundary.main()
                self.assertEqual(1, error.exception.code)
                self.assertIn("does not build the Link contract suite: " + target, output.getvalue())

    def test_required_fixture_names_cannot_be_supplied_only_by_comments(self) -> None:
        original_read_text = Path.read_text
        path = boundary.LINK_RENDER_CONTRACT_TEST
        source = path.read_text(encoding="utf-8")
        required = "testDiscardedContactChord"
        self.assertIn("void " + required + "()", source)
        changed = source.replace(required, "testRemovedDiscardedContract") + "\n// void " + required + "() {}"
        self.assertEqual([], self.fixture_inventory(changed))

        def read_changed(file: Path, *args, **kwargs):
            return changed if file == path else original_read_text(file, *args, **kwargs)

        output = io.StringIO()
        with patch.object(Path, "read_text", read_changed), contextlib.redirect_stderr(output):
            with self.assertRaises(SystemExit) as error:
                boundary.main()
        self.assertEqual(1, error.exception.code)
        self.assertIn("CPU Link geometry contract test is missing: " + required, output.getvalue())


if __name__ == "__main__":
    unittest.main(verbosity=2)
