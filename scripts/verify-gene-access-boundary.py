#!/usr/bin/env python3
"""Reject direct Gene storage access outside the compatibility storage layer."""

from __future__ import annotations

import re
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
NATIVE_ROOT = REPO_ROOT / "native"

# These units define or translate the current fixed-size compatibility storage.
# G-04 replaces their layout; all consumers must already use GeneView/GeneBuilder.
STORAGE_UNITS = {
    "native/core/Gene.cpp",
    "native/core/GeneAccess.h",
}

DIRECT_STORAGE_FIELD = re.compile(
    r"(?:\.|->)\s*(?:"
    r"mSplitMass|mSplitRatio|mSplitAngle|mChild1Angle|mChild2Angle|"
    r"mMassFlowPriority|mLinkStrength|mChild1GeneIndex|mChild2GeneIndex|"
    r"mIsMakeLink|mChild1KeepLinks|mChild2KeepLinks|mStayAlive|mIsInitial|"
    r"mChild1Mirror|mChild2Mirror|mCellType|mParametricValue|mInteger|"
    r"mVariableA|mVariableB|mVariableC|mSignalSubstanceChannelIndex|"
    r"mFormulaType|mFloatValues|mFlags|mCellTypeOrdinal|FLOAT_[A-Z_]+"
    r")\b"
)
GENE_VARIABLE = re.compile(
    r"\b(?:const\s+)?(?:Gene|GeneData)\s*(?:const\s*)?"
    r"(?:[&*]\s*)?([A-Za-z_]\w*)\b"
)
GENE_ARRAY_VARIABLE = re.compile(
    r"\bstd::array\s*<\s*(?:Gene|GeneData)\s*,[^;>{}]+>\s*"
    r"([A-Za-z_]\w*)\b"
)
RAW_GENE_METHODS = (
    "mutate",
    "mutatePointByWeightIndex",
    "fix",
    "isValidGeneValues",
)


def line_number(text: str, offset: int) -> int:
    return text.count("\n", 0, offset) + 1


def report_match(
    errors: list[str], relative_path: str, text: str, match: re.Match[str], reason: str
) -> None:
    line = text.splitlines()[line_number(text, match.start()) - 1].strip()
    errors.append(
        f"{relative_path}:{line_number(text, match.start())}: {reason}: {line}"
    )


def verify_source(path: Path, errors: list[str]) -> None:
    relative_path = path.relative_to(REPO_ROOT).as_posix()
    if relative_path in STORAGE_UNITS:
        return

    text = path.read_text(encoding="utf-8")
    for match in DIRECT_STORAGE_FIELD.finditer(text):
        report_match(
            errors,
            relative_path,
            text,
            match,
            "direct Gene storage field; use GeneView or GeneBuilder",
        )

    if "getActiveGene(" in text:
        for match in re.finditer(r"\bgetActiveGene\s*\(", text):
            report_match(
                errors,
                relative_path,
                text,
                match,
                "raw active-Gene access; use getActiveGeneView/editActiveGene",
            )

    gene_variables = set(GENE_VARIABLE.findall(text))
    gene_arrays = set(GENE_ARRAY_VARIABLE.findall(text))
    for variable in sorted(gene_variables):
        color_pattern = re.compile(
            rf"\b{re.escape(variable)}\s*(?:\.|->)\s*mColor\b"
        )
        for match in color_pattern.finditer(text):
            report_match(
                errors,
                relative_path,
                text,
                match,
                "direct Gene color storage; use GeneView or GeneBuilder",
            )
        method_pattern = re.compile(
            rf"\b{re.escape(variable)}\s*(?:\.|->)\s*"
            rf"(?:{'|'.join(RAW_GENE_METHODS)})\s*\("
        )
        for match in method_pattern.finditer(text):
            report_match(
                errors,
                relative_path,
                text,
                match,
                "raw Gene mutation/copy method; use GeneBuilder",
            )

    for variable in sorted(gene_arrays):
        color_pattern = re.compile(
            rf"\b{re.escape(variable)}\s*\[[^\]]+\]\s*\.\s*mColor\b"
        )
        for match in color_pattern.finditer(text):
            report_match(
                errors,
                relative_path,
                text,
                match,
                "direct Gene color storage; use GeneView or GeneBuilder",
            )

    for match in re.finditer(r"\bmGenome\s*\[[^\]]+\]\s*\[[^\]]+\]\s*\.\s*mColor\b", text):
        report_match(
            errors,
            relative_path,
            text,
            match,
            "direct Gene color storage; use GeneView or GeneBuilder",
        )


def main() -> None:
    errors: list[str] = []
    for path in sorted(NATIVE_ROOT.rglob("*")):
        if path.is_file() and path.suffix in {".cpp", ".h"}:
            verify_source(path, errors)

    if errors:
        print("Gene access boundary error:", file=sys.stderr)
        for error in errors:
            print(f"  {error}", file=sys.stderr)
        raise SystemExit(1)

    print("Gene access boundary verified")


if __name__ == "__main__":
    main()
