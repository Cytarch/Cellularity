#!/usr/bin/env python3
"""Measure candidate save-preview codecs without touching production code."""

from __future__ import annotations

import argparse
import io
import json
import math
import statistics
import time
from pathlib import Path
from typing import Any

from PIL import Image, ImageChops, __version__ as pillow_version


CODECS: tuple[tuple[str, str, dict[str, Any], str], ...] = (
    ("png", "PNG", {"compress_level": 6}, ".png"),
    (
        "jpeg-q85",
        "JPEG",
        {"quality": 85, "subsampling": 2, "optimize": False},
        ".jpg",
    ),
    ("webp-q85", "WEBP", {"quality": 85, "method": 4}, ".webp"),
    (
        "webp-lossless",
        "WEBP",
        {"lossless": True, "quality": 100, "method": 4},
        ".lossless.webp",
    ),
)


def percentile(values: list[int], fraction: float) -> int:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1)]


def quality(reference: Image.Image, decoded: Image.Image) -> dict[str, Any]:
    difference = ImageChops.difference(reference, decoded)
    histogram = difference.histogram()
    squared_error = sum(
        count * ((index % 256) ** 2)
        for index, count in enumerate(histogram)
    )
    samples = reference.width * reference.height * 3
    mse = squared_error / samples
    extrema = difference.getextrema()
    maximum_error = max(channel[1] for channel in extrema)
    return {
        "exact": mse == 0.0,
        "mse": mse,
        "rmse": math.sqrt(mse),
        "psnrDb": None if mse == 0.0 else 10.0 * math.log10((255.0**2) / mse),
        "maximumChannelError": maximum_error,
    }


def measure_codec(
    reference: Image.Image,
    label: str,
    image_format: str,
    save_options: dict[str, Any],
    extension: str,
    encode_repeats: int,
    decode_repeats: int,
    samples_directory: Path | None,
) -> dict[str, Any]:
    encoded = io.BytesIO()
    reference.save(encoded, format=image_format, **save_options)
    encoded_bytes = encoded.getvalue()

    encode_times: list[int] = []
    for _ in range(encode_repeats):
        destination = io.BytesIO()
        start = time.perf_counter_ns()
        reference.save(destination, format=image_format, **save_options)
        encode_times.append(time.perf_counter_ns() - start)

    decode_times: list[int] = []
    decoded: Image.Image | None = None
    for _ in range(decode_repeats):
        start = time.perf_counter_ns()
        with Image.open(io.BytesIO(encoded_bytes)) as image:
            image.load()
            decoded = image.convert("RGB")
        decode_times.append(time.perf_counter_ns() - start)
    assert decoded is not None

    if samples_directory is not None:
        samples_directory.mkdir(parents=True, exist_ok=True)
        sample_path = samples_directory / f"{reference.width}-{label}{extension}"
        sample_path.write_bytes(encoded_bytes)

    return {
        "codec": label,
        "pixelWidth": reference.width,
        "pixelHeight": reference.height,
        "encodedBytes": len(encoded_bytes),
        "decodedRgbBytes": reference.width * reference.height * 3,
        "encodeMedianNanoseconds": int(statistics.median(encode_times)),
        "encodeP95Nanoseconds": percentile(encode_times, 0.95),
        "decodeMedianNanoseconds": int(statistics.median(decode_times)),
        "decodeP95Nanoseconds": percentile(decode_times, 0.95),
        "quality": quality(reference, decoded),
        "saveOptions": save_options,
    }


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--samples-directory", type=Path)
    parser.add_argument("--encode-repeats", type=int, default=7)
    parser.add_argument("--decode-repeats", type=int, default=31)
    return parser.parse_args()


def main() -> None:
    arguments = parse_arguments()
    if arguments.encode_repeats < 1 or arguments.decode_repeats < 1:
        raise ValueError("Repeat counts must be positive")

    with Image.open(arguments.input) as source_image:
        source_image.load()
        source = source_image.convert("RGB")

    measurements: list[dict[str, Any]] = []
    for size in (256, 512, 1024):
        reference = source.resize((size, size), Image.Resampling.LANCZOS)
        for label, image_format, options, extension in CODECS:
            measurements.append(
                measure_codec(
                    reference,
                    label,
                    image_format,
                    options,
                    extension,
                    arguments.encode_repeats,
                    arguments.decode_repeats,
                    arguments.samples_directory,
                )
            )

    report = {
        "schema": "cellularity/save-preview-candidate-lab",
        "schemaRevision": 1,
        "source": str(arguments.input),
        "sourcePixelWidth": source.width,
        "sourcePixelHeight": source.height,
        "sourceRgbBytes": source.width * source.height * 3,
        "pillowVersion": pillow_version,
        "encodeRepeats": arguments.encode_repeats,
        "decodeRepeats": arguments.decode_repeats,
        "measurements": measurements,
    }
    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    arguments.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
