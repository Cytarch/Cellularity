#!/usr/bin/env python3

from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import math
import re
import sys
import xml.etree.ElementTree as ElementTree


REPO_ROOT = Path(__file__).resolve().parents[1]
RESOURCE_ROOTS = {
    "app": REPO_ROOT / "app/src/main/res",
    "shared": REPO_ROOT / "shared/src/commonMain/composeResources",
}
RESOURCE_FILE_LAYOUT = {
    "app": {
        "diagnostics_strings.xml",
    },
    "shared": {
        "about_strings.xml",
        "cell_properties_strings.xml",
        "common_strings.xml",
        "diagnostics_strings.xml",
        "file_collection_strings.xml",
        "freezer_strings.xml",
        "gene_bank_strings.xml",
        "genome_editor_strings.xml",
        "main_menu_strings.xml",
        "microscope_strings.xml",
        "mutation_weights_strings.xml",
        "play_strings.xml",
        "settings_strings.xml",
        "sharing_strings.xml",
        "simulation_strings.xml",
        "substrate_info_strings.xml",
    },
}
RESOURCE_KEY_PREFIXES = {
    "app": {
        "diagnostics_strings.xml": ("diagnostics_", "recovery_"),
        "strings_untranslated.xml": ("app_",),
    },
    "shared": {
        "about_strings.xml": ("about_",),
        "cell_properties_strings.xml": ("cell_property_",),
        "common_strings.xml": ("common_",),
        "diagnostics_strings.xml": ("diagnostics_",),
        "file_collection_strings.xml": ("file_collection_", "file_operation_"),
        "freezer_strings.xml": ("freezer_",),
        "gene_bank_strings.xml": ("gene_bank_",),
        "genome_editor_strings.xml": ("genome_editor_",),
        "main_menu_strings.xml": ("main_menu_",),
        "microscope_strings.xml": ("microscope_",),
        "mutation_weights_strings.xml": ("mutation_weight_", "mutation_weights_"),
        "play_strings.xml": ("play_",),
        "settings_strings.xml": ("settings_",),
        "sharing_strings.xml": ("sharing_",),
        "simulation_strings.xml": ("simulation_",),
        "substrate_info_strings.xml": ("substrate_",),
        "strings_untranslated.xml": ("settings_",),
    },
}
UNTRANSLATED_RESOURCE_FILE = "strings_untranslated.xml"
EXPECTED_LOCALES = ("es", "ko", "pl", "pt", "ru")
RELEASE_LANGUAGE_TAGS = ("en",) + EXPECTED_LOCALES
ANDROID_BOOTSTRAP_RESOURCE_NAMES = {
    "app_name",
    "diagnostics_export_chooser_title",
    "recovery_acknowledge_failed_message",
    "recovery_continue_to_app_action",
    "recovery_description",
    "recovery_diagnostics_export_failed_message",
    "recovery_export_diagnostics_action",
    "recovery_latest_report_format",
    "recovery_privacy_notice",
    "recovery_report_count",
    "recovery_title",
}
REQUIRED_PLURAL_QUANTITIES = {
    "default": {"one", "other"},
    "es": {"one", "many", "other"},
    "ko": {"other"},
    "pl": {"one", "few", "many", "other"},
    "pt": {"one", "many", "other"},
    "ru": {"one", "few", "many", "other"},
}
RESOURCE_TAGS = {"plurals", "string", "string-array"}
SHARED_RESOURCE_IMPORT = re.compile(
    r"^import com\.cytarch\.cellularity\.shared\.generated\.resources\.([A-Za-z0-9_]+)$",
    re.MULTILINE,
)
SHARED_RES_CLASS_IMPORT = re.compile(
    r"^import com\.cytarch\.cellularity\.shared\.generated\.resources\.Res"
    r"(?:\s+as\s+([A-Za-z_][A-Za-z0-9_]*))?$",
    re.MULTILINE,
)
SHARED_RESOURCE_WILDCARD_IMPORT = re.compile(
    r"^import com\.cytarch\.cellularity\.shared\.generated\.resources\.\*$",
    re.MULTILINE,
)
FORMAT_TOKEN = re.compile(
    r"%(?:(?P<index>\d+)\$)?(?P<flags>[-#+ 0,(<]*)"
    r"\d*(?:\.\d+)?(?P<date>[tT])?(?P<conversion>[a-zA-Z%])"
)
TOOLS_NAMESPACE = "http://schemas.android.com/tools"
TOOLS_IGNORE_ATTRIBUTE = f"{{{TOOLS_NAMESPACE}}}ignore"
ANDROID_NAMESPACE = "http://schemas.android.com/apk/res/android"
ANDROID_TEXT_ATTRIBUTES = {
    "contentDescription",
    "dialogTitle",
    "hint",
    "label",
    "summary",
    "text",
    "title",
}
ANDROID_STYLE_TEXT_ITEMS = {
    "android:contentDescription",
    "android:hint",
    "android:summary",
    "android:text",
    "android:title",
}
SOURCE_ROOTS = (
    REPO_ROOT / "app/src/main/java",
    REPO_ROOT / "app/src/main/kotlin",
    REPO_ROOT / "shared/src/commonMain/kotlin",
    REPO_ROOT / "shared/src/androidMain/kotlin",
)
SOURCE_SUFFIXES = {".java", ".kt"}
ANDROID_XML_ROOTS = (
    REPO_ROOT / "app/src/main/res",
    REPO_ROOT / "shared/src/androidMain/res",
)
ANDROID_MANIFESTS = (
    REPO_ROOT / "app/src/main/AndroidManifest.xml",
    REPO_ROOT / "shared/src/androidMain/AndroidManifest.xml",
)
LANGUAGE_MODEL_PATH = (
    REPO_ROOT
    / "shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/settings/CellularitySettings.kt"
)
UI_ASSIGNMENT = re.compile(
    r"\b(?:body|buttonText|caption|contentDescription|description|emptyText|errorText|"
    r"headline|hint|label|message|onClickLabel|paneTitle|placeholder|stateDescription|"
    r"subject|subtitle|summary|supportingText|text|title|valueText)\s*=\s*$",
    re.DOTALL,
)
UI_CALL_PATTERNS = (
    (
        "toast-or-snackbar",
        re.compile(
            r"\b(?:Toast\s*\.\s*makeText|Snackbar\s*\.\s*make)"
            r"\s*\([^,]{0,500},\s*$",
            re.DOTALL,
        ),
    ),
    (
        "compose-text",
        re.compile(
            r"\b(?:AnnotatedString|BasicText|Text)\s*\(\s*(?:text\s*=\s*)?$",
            re.DOTALL,
        ),
    ),
    (
        "android-text-setter",
        re.compile(
            r"\b(?:setContentDescription|setHint|setMessage|setNegativeButton|"
            r"setNeutralButton|setPositiveButton|setText|setTitle)\s*\(\s*$",
            re.DOTALL,
        ),
    ),
)

# Every direct source literal reaching a UI sink must be identified by exact file,
# sink, value, and occurrence count. Natural-language copy is never allowlisted.
UI_LITERAL_ALLOWLIST = {
    (
        "shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/components/SliderCard.kt",
        "compose-text",
        "0.0",
    ): (1, "locale-neutral editable-number example"),
}

HTML_TAG = re.compile(r"</?[A-Za-z][^>]*>")
BACKSLASH_ESCAPE = re.compile(r"\\(?:[nrt'\"\\]|u[0-9A-Fa-f]{4})")
PSEUDO_ACCENTS = str.maketrans(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz",
    "ÅƁÇÐËƑĞĦÏĴĶĿḾŃÖƤQŘŠŦÜṼŴẊŸŽåƀçðëƒğħïĵķŀḿńöƥqřšŧüṽŵẋÿž",
)


@dataclass(frozen=True)
class SourceLiteral:
    line: int
    start: int
    end: int
    value: str


@dataclass(frozen=True)
class UiLiteralFinding:
    path: str
    line: int
    sink: str
    value: str


def fail(messages: list[str]) -> None:
    for message in messages:
        print(f"locale resource error: {message}", file=sys.stderr)
    raise SystemExit(1)


def element_text(element: ElementTree.Element) -> str:
    return "".join(element.itertext())


def is_translatable(element: ElementTree.Element) -> bool:
    return element.get("translatable") != "false"


def placeholder_signature(text: str) -> Counter[tuple[int, str]]:
    signature: Counter[tuple[int, str]] = Counter()
    implicit_index = 0
    previous_index: int | None = None
    for match in FORMAT_TOKEN.finditer(text):
        conversion = match.group("conversion")
        if conversion in {"%", "n"}:
            continue
        if match.group("index") is not None:
            argument_index = int(match.group("index"))
        elif "<" in match.group("flags") and previous_index is not None:
            argument_index = previous_index
        else:
            implicit_index += 1
            argument_index = implicit_index
        previous_index = argument_index
        conversion_type = (match.group("date") or "") + conversion.lower()
        signature[(argument_index, conversion_type)] += 1
    return signature


def has_unindexed_multi_argument_format(text: str) -> bool:
    matches = [
        match
        for match in FORMAT_TOKEN.finditer(text)
        if match.group("conversion") not in {"%", "n"}
    ]
    argument_indices = {argument_index for argument_index, _ in placeholder_signature(text)}
    if len(argument_indices) <= 1:
        return False
    return any(
        match.group("index") is None and "<" not in match.group("flags")
        for match in matches
    )


def unsupported_indexed_compose_formats(text: str) -> list[str]:
    """Compose Resources interpolates indexed string and integer arguments only."""
    return sorted(
        {
            match.group(0)
            for match in FORMAT_TOKEN.finditer(text)
            if match.group("index") is not None
            and (
                match.group("date") is not None
                or match.group("conversion").lower() not in {"d", "s"}
            )
        }
    )


def resource_directories(root: Path) -> list[tuple[str, Path]]:
    return [("default", root / "values")] + [
        (locale, root / f"values-{locale}") for locale in EXPECTED_LOCALES
    ]


def resource_key_prefix_error(
    owner: str,
    locale: str,
    filename: str,
    tag: str,
    name: str,
) -> str | None:
    allowed_prefixes = RESOURCE_KEY_PREFIXES[owner].get(filename)
    if allowed_prefixes is None or name.startswith(allowed_prefixes):
        return None
    return (
        f"{owner}/{locale} places {tag}/{name} in {filename}, "
        f"which requires prefix {allowed_prefixes}"
    )


def verify_resource_file_layout(owner: str, root: Path) -> list[str]:
    errors: list[str] = []
    expected_files = RESOURCE_FILE_LAYOUT[owner]
    locations_by_locale: dict[str, dict[tuple[str, str], str]] = {}

    for locale, directory in resource_directories(root):
        locations: dict[tuple[str, str], str] = {}
        locations_by_locale[locale] = locations
        actual_files: set[str] = set()
        if not directory.is_dir():
            continue

        for xml_path in sorted(directory.glob("*.xml")):
            if xml_path.name != UNTRANSLATED_RESOURCE_FILE and (
                xml_path.name == "strings.xml"
                or xml_path.name.endswith("_strings.xml")
            ):
                actual_files.add(xml_path.name)
            try:
                xml_root = ElementTree.parse(xml_path).getroot()
            except ElementTree.ParseError:
                continue

            for element in xml_root:
                if element.tag not in RESOURCE_TAGS:
                    continue
                name = element.get("name")
                if name:
                    prefix_error = resource_key_prefix_error(
                        owner,
                        locale,
                        xml_path.name,
                        element.tag,
                        name,
                    )
                    if prefix_error is not None:
                        errors.append(prefix_error)
                if xml_path.name == UNTRANSLATED_RESOURCE_FILE:
                    continue
                actual_files.add(xml_path.name)
                if name:
                    locations[(element.tag, name)] = xml_path.name

        missing = sorted(expected_files - actual_files)
        unexpected = sorted(actual_files - expected_files)
        if missing or unexpected:
            errors.append(
                f"{owner}/{locale} has wrong localized resource files: "
                f"missing={missing}, unexpected={unexpected}"
            )

    default_locations = locations_by_locale.get("default", {})
    for locale in EXPECTED_LOCALES:
        for key, filename in locations_by_locale.get(locale, {}).items():
            default_filename = default_locations.get(key)
            if default_filename is not None and filename != default_filename:
                tag, name = key
                errors.append(
                    f"{owner}/{locale} places {tag}/{name} in {filename}; "
                    f"the default resource uses {default_filename}"
                )
    return errors


def load_resources(
    owner: str, root: Path
) -> tuple[dict[str, dict[tuple[str, str], ElementTree.Element]], list[str]]:
    locales: dict[str, dict[tuple[str, str], ElementTree.Element]] = {}
    errors: list[str] = []
    for locale, directory in resource_directories(root):
        resources: dict[tuple[str, str], ElementTree.Element] = {}
        locales[locale] = resources
        if not directory.is_dir():
            errors.append(f"{owner} is missing resource directory {directory.name}")
            continue
        for xml_path in sorted(directory.glob("*.xml")):
            try:
                xml_root = ElementTree.parse(xml_path).getroot()
            except ElementTree.ParseError as error:
                errors.append(f"{xml_path.relative_to(REPO_ROOT)} is invalid XML: {error}")
                continue
            for element in xml_root:
                if element.tag not in RESOURCE_TAGS:
                    continue
                name = element.get("name")
                if not name:
                    errors.append(
                        f"{xml_path.relative_to(REPO_ROOT)} contains an unnamed {element.tag}"
                    )
                    continue
                key = (element.tag, name)
                if key in resources:
                    errors.append(f"{owner}/{locale} defines {element.tag}/{name} more than once")
                resources[key] = element
    return locales, errors


def verify_resource_internals(
    owner: str,
    locale: str,
    resources: dict[tuple[str, str], ElementTree.Element],
) -> list[str]:
    errors: list[str] = []
    expected_quantities = REQUIRED_PLURAL_QUANTITIES[locale]
    for (tag, name), element in resources.items():
        text_elements = [element] if tag == "string" else element.findall("item")
        if element.get("formatted") != "false":
            for index, text_element in enumerate(text_elements):
                text = element_text(text_element)
                if has_unindexed_multi_argument_format(text):
                    suffix = "" if tag == "string" else f"[{index}]"
                    errors.append(
                        f"{owner}/{locale} has unindexed multi-argument placeholders in "
                        f"{tag}/{name}{suffix}"
                    )
                if owner == "shared":
                    unsupported_formats = unsupported_indexed_compose_formats(text)
                    if unsupported_formats:
                        suffix = "" if tag == "string" else f"[{index}]"
                        errors.append(
                            f"{owner}/{locale} uses formats unsupported by Compose Resources "
                            f"in {tag}/{name}{suffix}: {unsupported_formats}"
                        )

        if tag != "plurals":
            continue
        quantities = [item.get("quantity") for item in element.findall("item")]
        repeated = sorted(
            quantity
            for quantity, count in Counter(quantities).items()
            if quantity is not None and count > 1
        )
        if repeated:
            errors.append(
                f"{owner}/{locale} repeats quantities {repeated} in plurals/{name}"
            )
        actual_quantities = {quantity for quantity in quantities if quantity is not None}
        if actual_quantities != expected_quantities:
            missing = sorted(expected_quantities - actual_quantities)
            extra = sorted(actual_quantities - expected_quantities)
            errors.append(
                f"{owner}/{locale} has wrong quantities in plurals/{name}: "
                f"missing={missing}, extra={extra}"
            )
    return errors


def verify_locale_against_default(
    owner: str,
    locale: str,
    localized: dict[tuple[str, str], ElementTree.Element],
    default: dict[tuple[str, str], ElementTree.Element],
) -> list[str]:
    errors: list[str] = []
    required = {key for key, element in default.items() if is_translatable(element)}
    for tag, name in sorted(required - set(localized)):
        errors.append(f"{owner}/{locale} is missing {tag}/{name}")

    for key, localized_element in localized.items():
        tag, name = key
        default_element = default.get(key)
        if default_element is None:
            errors.append(f"{owner}/{locale} has {tag}/{name}, but the default resource is missing")
            continue
        if not is_translatable(default_element):
            errors.append(f"{owner}/{locale} overrides non-translatable {tag}/{name}")
            continue

        if tag == "string":
            default_unformatted = default_element.get("formatted") == "false"
            localized_unformatted = localized_element.get("formatted") == "false"
            if default_unformatted != localized_unformatted:
                errors.append(f"{owner}/{locale} changes formatted= for string/{name}")
            elif not default_unformatted and (
                placeholder_signature(element_text(localized_element))
                != placeholder_signature(element_text(default_element))
            ):
                errors.append(f"{owner}/{locale} has incompatible placeholders in string/{name}")
            continue

        if tag == "string-array":
            default_items = default_element.findall("item")
            localized_items = localized_element.findall("item")
            if len(localized_items) != len(default_items):
                errors.append(
                    f"{owner}/{locale} changes string-array/{name} length "
                    f"from {len(default_items)} to {len(localized_items)}"
                )
                continue
            for index, (default_item, localized_item) in enumerate(
                zip(default_items, localized_items, strict=True)
            ):
                if placeholder_signature(element_text(localized_item)) != placeholder_signature(
                    element_text(default_item)
                ):
                    errors.append(
                        f"{owner}/{locale} has incompatible placeholders in "
                        f"string-array/{name}[{index}]"
                    )
            continue

        default_quantities = {
            item.get("quantity"): item for item in default_element.findall("item")
        }
        for item in localized_element.findall("item"):
            quantity = item.get("quantity")
            reference = default_quantities.get(quantity, default_quantities.get("other"))
            if reference is None:
                errors.append(f"{owner}/default has no fallback quantity for plurals/{name}")
            elif placeholder_signature(element_text(item)) != placeholder_signature(
                element_text(reference)
            ):
                errors.append(
                    f"{owner}/{locale} has incompatible placeholders in "
                    f"plurals/{name}[{quantity}]"
                )
    return errors


def verify_cross_owner_resources(
    app: dict[str, dict[tuple[str, str], ElementTree.Element]],
    shared: dict[str, dict[tuple[str, str], ElementTree.Element]],
) -> list[str]:
    app_names = {name for _, name in app["default"]}
    shared_names = {name for _, name in shared["default"]}
    return [
        f"resource/{name} has both app and shared owners"
        for name in sorted(app_names & shared_names)
    ]


def verify_android_bootstrap_resource_boundary(
    app: dict[str, dict[tuple[str, str], ElementTree.Element]],
) -> list[str]:
    errors: list[str] = []
    actual_names = {name for _, name in app["default"]}
    if actual_names != ANDROID_BOOTSTRAP_RESOURCE_NAMES:
        errors.append(
            "Android string resources must be limited to the manifest and recovery bootstrap: "
            f"missing={sorted(ANDROID_BOOTSTRAP_RESOURCE_NAMES - actual_names)}, "
            f"unexpected={sorted(actual_names - ANDROID_BOOTSTRAP_RESOURCE_NAMES)}"
        )

    forbidden_apis = (
        "androidx.compose.ui.res.pluralStringResource",
        "androidx.compose.ui.res.stringArrayResource",
        "androidx.compose.ui.res.stringResource",
        "LocalResources.current.getStringArray",
    )
    for source_root in (REPO_ROOT / "app/src/main/java", REPO_ROOT / "app/src/main/kotlin"):
        for source_path in source_root.rglob("*.kt"):
            source = source_path.read_text(encoding="utf-8")
            for forbidden_api in forbidden_apis:
                if forbidden_api in source:
                    errors.append(
                        f"{source_path.relative_to(REPO_ROOT)} uses Android UI text API "
                        f"{forbidden_api}; use shared Compose Resources"
                    )
    return errors


def verify_release_language_selector(
    shared: dict[str, dict[tuple[str, str], ElementTree.Element]],
) -> list[str]:
    errors: list[str] = []
    language_names = shared["default"].get(("string-array", "settings_language_names"))
    if language_names is None:
        errors.append("shared/default is missing string-array/settings_language_names")
    else:
        names = tuple(element_text(item).strip() for item in language_names.findall("item"))
        if is_translatable(language_names):
            errors.append(
                "shared/default string-array/settings_language_names must be non-translatable"
            )
        if len(names) != len(RELEASE_LANGUAGE_TAGS):
            errors.append(
                "release language selector has "
                f"{len(names)} names for {len(RELEASE_LANGUAGE_TAGS)} supported languages"
            )

    if not LANGUAGE_MODEL_PATH.is_file():
        errors.append(f"release language model is missing: {LANGUAGE_MODEL_PATH.relative_to(REPO_ROOT)}")
        return errors

    model_text = LANGUAGE_MODEL_PATH.read_text(encoding="utf-8")
    enum_match = re.search(
        r"enum\s+class\s+CellularityLanguage\b[^\{]*\{(?P<entries>.*?);",
        model_text,
        re.DOTALL,
    )
    if enum_match is None:
        errors.append("cannot parse CellularityLanguage release entries")
        return errors
    actual_tags = tuple(
        re.findall(
            r"\b[A-Z][A-Z0-9_]*\s*\(\s*\"([^\"]+)\"\s*\)\s*,?",
            enum_match.group("entries"),
        )
    )
    if actual_tags != RELEASE_LANGUAGE_TAGS:
        errors.append(
            "release language selector tags changed: "
            f"expected={list(RELEASE_LANGUAGE_TAGS)}, actual={list(actual_tags)}"
        )
    return errors


def verify_shared_resource_references(
    shared: dict[str, dict[tuple[str, str], ElementTree.Element]],
) -> list[str]:
    resource_names = {name for _, name in shared["default"]}
    for resource_type in ("drawable", "font"):
        directory = RESOURCE_ROOTS["shared"] / resource_type
        resource_names.update(path.stem for path in directory.glob("*") if path.is_file())

    imported_names: set[str] = set()
    referenced_names: set[str] = set()
    for source_root in (REPO_ROOT / "shared/src", REPO_ROOT / "app/src/main"):
        for source_path in source_root.rglob("*.kt"):
            source = source_path.read_text(encoding="utf-8")
            imported_names.update(SHARED_RESOURCE_IMPORT.findall(source))
            aliases = {
                match.group(1) or "Res"
                for match in SHARED_RES_CLASS_IMPORT.finditer(source)
            }
            if SHARED_RESOURCE_WILDCARD_IMPORT.search(source):
                aliases.add("Res")
            _, masked_source = source_literals_and_mask(source)
            for alias in aliases:
                reference = re.compile(
                    rf"\b{re.escape(alias)}\s*\.\s*"
                    r"(?:array|drawable|font|plurals|string)\s*\.\s*"
                    r"([A-Za-z0-9_]+)\b"
                )
                referenced_names.update(reference.findall(masked_source))
    imported_names.discard("Res")

    errors: list[str] = []
    unreferenced = sorted(resource_names - referenced_names)
    missing = sorted((imported_names | referenced_names) - resource_names)
    unused_imports = sorted(imported_names - referenced_names)
    if unreferenced:
        errors.append(f"shared resources have no qualified consumer: {unreferenced}")
    if missing:
        errors.append(f"generated-resource references have no shared resource: {missing}")
    if unused_imports:
        errors.append(f"generated-resource imports have no qualified use: {unused_imports}")
    return errors


def verify_no_missing_translation_suppression() -> list[str]:
    errors: list[str] = []
    for resource_root in RESOURCE_ROOTS.values():
        for xml_path in sorted(resource_root.glob("values*/*.xml")):
            try:
                root = ElementTree.parse(xml_path).getroot()
            except ElementTree.ParseError:
                continue
            for element in root.iter():
                ignored = element.get(TOOLS_IGNORE_ATTRIBUTE, "")
                ignored_checks = {
                    check.strip() for check in re.split(r"[, ]+", ignored) if check.strip()
                }
                if "MissingTranslation" in ignored_checks:
                    errors.append(
                        f"{xml_path.relative_to(REPO_ROOT)} suppresses MissingTranslation"
                    )
    return errors


def mask_range(mask: list[str], start: int, end: int) -> None:
    for index in range(start, end):
        if mask[index] != "\n":
            mask[index] = " "


def source_literals_and_mask(text: str) -> tuple[list[SourceLiteral], str]:
    literals: list[SourceLiteral] = []
    mask = list(text)
    index = 0
    line = 1
    size = len(text)
    while index < size:
        if text[index] == "\n":
            line += 1
            index += 1
            continue
        if text.startswith("//", index):
            end = text.find("\n", index + 2)
            end = size if end < 0 else end
            mask_range(mask, index, end)
            index = end
            continue
        if text.startswith("/*", index):
            start = index
            depth = 1
            index += 2
            while index < size and depth:
                if text.startswith("/*", index):
                    depth += 1
                    index += 2
                elif text.startswith("*/", index):
                    depth -= 1
                    index += 2
                else:
                    if text[index] == "\n":
                        line += 1
                    index += 1
            mask_range(mask, start, index)
            continue
        if text.startswith('"""', index):
            start = index
            start_line = line
            end_marker = text.find('"""', index + 3)
            content_end = size if end_marker < 0 else end_marker
            end = size if end_marker < 0 else end_marker + 3
            value = text[index + 3 : content_end]
            literals.append(SourceLiteral(start_line, start, end, value))
            line += text[start:end].count("\n")
            mask_range(mask, start, end)
            index = end
            continue
        if text[index] == '"':
            start = index
            start_line = line
            cursor = index + 1
            value: list[str] = []
            while cursor < size:
                if text[cursor] == "\\" and cursor + 1 < size:
                    value.append(text[cursor : cursor + 2])
                    cursor += 2
                elif text[cursor] == '"':
                    cursor += 1
                    break
                else:
                    if text[cursor] == "\n":
                        line += 1
                    value.append(text[cursor])
                    cursor += 1
            literals.append(SourceLiteral(start_line, start, cursor, "".join(value)))
            mask_range(mask, start, cursor)
            index = cursor
            continue
        if text[index] == "'":
            start = index
            cursor = index + 1
            while cursor < size:
                if text[cursor] == "\\" and cursor + 1 < size:
                    cursor += 2
                elif text[cursor] == "'":
                    cursor += 1
                    break
                else:
                    if text[cursor] == "\n":
                        line += 1
                    cursor += 1
            mask_range(mask, start, cursor)
            index = cursor
            continue
        index += 1
    return literals, "".join(mask)


def classify_ui_literal(masked_prefix: str) -> str | None:
    for sink, pattern in UI_CALL_PATTERNS:
        if pattern.search(masked_prefix):
            return sink
    if UI_ASSIGNMENT.search(masked_prefix):
        return "ui-text-parameter"
    return None


def find_source_ui_literals(relative_path: str, text: str) -> list[UiLiteralFinding]:
    literals, mask = source_literals_and_mask(text)
    findings: list[UiLiteralFinding] = []
    for literal in literals:
        prefix = mask[max(0, literal.start - 1200) : literal.start]
        sink = classify_ui_literal(prefix)
        if sink is not None:
            findings.append(
                UiLiteralFinding(relative_path, literal.line, sink, literal.value)
            )
    return findings


def verify_source_ui_literals() -> tuple[list[str], int]:
    findings: list[UiLiteralFinding] = []
    for source_root in SOURCE_ROOTS:
        for source_path in sorted(
            path
            for path in source_root.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        ):
            relative_path = source_path.relative_to(REPO_ROOT).as_posix()
            findings.extend(
                find_source_ui_literals(
                    relative_path,
                    source_path.read_text(encoding="utf-8"),
                )
            )

    actual = Counter((item.path, item.sink, item.value) for item in findings)
    expected = Counter(
        {key: expected_count for key, (expected_count, _) in UI_LITERAL_ALLOWLIST.items()}
    )
    errors: list[str] = []
    for finding in findings:
        key = (finding.path, finding.sink, finding.value)
        if key not in UI_LITERAL_ALLOWLIST:
            errors.append(
                f"direct UI literal at {finding.path}:{finding.line} "
                f"({finding.sink}): {finding.value!r}"
            )
    for key in sorted(set(actual) | set(expected)):
        if key in UI_LITERAL_ALLOWLIST and actual[key] != expected[key]:
            _, reason = UI_LITERAL_ALLOWLIST[key]
            errors.append(
                f"UI literal allowlist count changed for {key}: "
                f"expected {expected[key]}, found {actual[key]} ({reason})"
            )
    return errors, len(findings)


def find_android_xml_text(root: ElementTree.Element, relative_path: str) -> list[str]:
    findings: list[str] = []
    for element in root.iter():
        for attribute_name, value in element.attrib.items():
            if not attribute_name.startswith(f"{{{ANDROID_NAMESPACE}}}"):
                continue
            local_name = attribute_name.split("}", 1)[1]
            if (
                local_name in ANDROID_TEXT_ATTRIBUTES
                and value
                and not value.startswith(("@", "?"))
            ):
                findings.append(f"{relative_path}:{local_name}={value!r}")
        if (
            element.tag == "item"
            and element.get("name") in ANDROID_STYLE_TEXT_ITEMS
            and (element.text or "").strip()
            and not (element.text or "").strip().startswith(("@", "?"))
        ):
            findings.append(
                f"{relative_path}:{element.get('name')}={(element.text or '').strip()!r}"
            )
    return findings


def direct_android_xml_text() -> list[str]:
    findings: list[str] = []
    paths: list[Path] = []
    for resource_root in ANDROID_XML_ROOTS:
        if resource_root.is_dir():
            paths.extend(sorted(resource_root.rglob("*.xml")))
    paths.extend(manifest for manifest in ANDROID_MANIFESTS if manifest.is_file())
    for xml_path in paths:
        try:
            root = ElementTree.parse(xml_path).getroot()
        except ElementTree.ParseError:
            continue
        findings.extend(
            find_android_xml_text(root, xml_path.relative_to(REPO_ROOT).as_posix())
        )
    return findings


def protected_pseudo_spans(text: str) -> list[tuple[int, int]]:
    spans = [match.span() for match in FORMAT_TOKEN.finditer(text)]
    spans.extend(match.span() for match in HTML_TAG.finditer(text))
    spans.extend(match.span() for match in BACKSLASH_ESCAPE.finditer(text))
    spans.sort()
    merged: list[tuple[int, int]] = []
    for start, end in spans:
        if merged and start < merged[-1][1]:
            previous_start, previous_end = merged[-1]
            merged[-1] = (previous_start, max(previous_end, end))
        else:
            merged.append((start, end))
    return merged


def pseudo_localize(text: str) -> tuple[str, int]:
    spans = protected_pseudo_spans(text)
    output = ["⟦"]
    cursor = 0
    visible_letters = 0
    for start, end in spans:
        plain = text[cursor:start]
        visible_letters += sum(character.isalpha() for character in plain)
        output.append(plain.translate(PSEUDO_ACCENTS))
        output.append(text[start:end])
        cursor = end
    plain = text[cursor:]
    visible_letters += sum(character.isalpha() for character in plain)
    output.append(plain.translate(PSEUDO_ACCENTS))
    padding = max(4, math.ceil(visible_letters * 0.35))
    output.append(" " + "~" * padding + "⟧")
    return "".join(output), visible_letters


def verify_pseudo_locale_expansion(
    loaded: dict[str, dict[str, dict[tuple[str, str], ElementTree.Element]]],
) -> tuple[list[str], int]:
    errors: list[str] = []
    expanded_count = 0
    for owner, locales in loaded.items():
        for (tag, name), element in locales["default"].items():
            if not is_translatable(element):
                continue
            text_elements = [element] if tag == "string" else element.findall("item")
            for index, text_element in enumerate(text_elements):
                original = element_text(text_element)
                pseudo, visible_letters = pseudo_localize(original)
                if visible_letters == 0:
                    continue
                expanded_count += 1
                suffix = "" if tag == "string" else f"[{index}]"
                if placeholder_signature(pseudo) != placeholder_signature(original):
                    errors.append(
                        f"pseudo-locale changes placeholders in {owner}/{tag}/{name}{suffix}"
                    )
                minimum_growth = max(4, math.ceil(visible_letters * 0.35))
                wrapper_length = len("⟦ ⟧")
                if len(pseudo) - len(original) - wrapper_length < minimum_growth:
                    errors.append(
                        f"pseudo-locale did not expand {owner}/{tag}/{name}{suffix}"
                    )
    for root in RESOURCE_ROOTS.values():
        for forbidden in ("values-en-rXA", "values-ar-rXB"):
            if (root / forbidden).exists():
                errors.append(
                    f"{(root / forbidden).relative_to(REPO_ROOT)} must be debug-generated, "
                    "not a release source locale"
                )
    return errors, expanded_count


def negative_control_errors() -> list[str]:
    failures: list[str] = []
    default_probe = {
        ("string", "probe"): ElementTree.fromstring(
            '<string name="probe">Value %1$s / %2$d</string>'
        )
    }
    missing_errors = verify_locale_against_default("probe", "es", {}, default_probe)
    if not any("is missing string/probe" in error for error in missing_errors):
        failures.append("missing-key negative control did not fail")

    wrong_placeholder = {
        ("string", "probe"): ElementTree.fromstring(
            '<string name="probe">Valor %1$s / %2$s</string>'
        )
    }
    placeholder_errors = verify_locale_against_default(
        "probe", "es", wrong_placeholder, default_probe
    )
    if not any("incompatible placeholders" in error for error in placeholder_errors):
        failures.append("placeholder negative control did not fail")

    orphan_localized = {
        ("string", "probe"): ElementTree.fromstring(
            '<string name="probe">Valor %1$s / %2$d</string>'
        ),
        ("string", "orphan"): ElementTree.fromstring(
            '<string name="orphan">Orphan</string>'
        ),
    }
    orphan_errors = verify_locale_against_default(
        "probe", "es", orphan_localized, default_probe
    )
    if not any("default resource is missing" in error for error in orphan_errors):
        failures.append("orphan-key negative control did not fail")

    unindexed_probe = {
        ("string", "probe"): ElementTree.fromstring(
            '<string name="probe">Value %s / %d</string>'
        )
    }
    unindexed_errors = verify_resource_internals("probe", "es", unindexed_probe)
    if not any("unindexed multi-argument placeholders" in error for error in unindexed_errors):
        failures.append("placeholder-order negative control did not fail")

    unsupported_compose_probe = {
        ("string", "probe"): ElementTree.fromstring(
            '<string name="probe">Value %1$.2f</string>'
        )
    }
    unsupported_compose_errors = verify_resource_internals(
        "shared", "es", unsupported_compose_probe
    )
    if not any("unsupported by Compose Resources" in error for error in unsupported_compose_errors):
        failures.append("Compose-format negative control did not fail")

    plural_probe = {
        ("plurals", "probe_count"): ElementTree.fromstring(
            '<plurals name="probe_count"><item quantity="other">%1$d items</item></plurals>'
        )
    }
    plural_errors = verify_resource_internals("probe", "es", plural_probe)
    if not any("wrong quantities" in error for error in plural_errors):
        failures.append("plural-quantity negative control did not fail")

    duplicate_plural_probe = {
        ("plurals", "probe_count"): ElementTree.fromstring(
            '<plurals name="probe_count">'
            '<item quantity="one">%1$d item</item>'
            '<item quantity="one">%1$d item</item>'
            '<item quantity="many">%1$d items</item>'
            '<item quantity="other">%1$d items</item>'
            '</plurals>'
        )
    }
    duplicate_plural_errors = verify_resource_internals(
        "probe", "es", duplicate_plural_probe
    )
    if not any("repeats quantities" in error for error in duplicate_plural_errors):
        failures.append("duplicate-plural-quantity negative control did not fail")

    array_default = {
        ("string-array", "probe_array"): ElementTree.fromstring(
            '<string-array name="probe_array"><item>A</item><item>B</item></string-array>'
        )
    }
    array_localized = {
        ("string-array", "probe_array"): ElementTree.fromstring(
            '<string-array name="probe_array"><item>A</item></string-array>'
        )
    }
    array_errors = verify_locale_against_default(
        "probe", "es", array_localized, array_default
    )
    if not any("changes string-array/probe_array length" in error for error in array_errors):
        failures.append("array-shape negative control did not fail")

    direct_ui = find_source_ui_literals(
        "app/src/main/java/com/cytarch/cellularity/Probe.kt",
        """fun Probe() {
    Text("Untranslated UI text")
    Image(contentDescription = "Untranslated description")
    Toast.makeText(context, "Untranslated toast", Toast.LENGTH_LONG).show()
}""",
    )
    expected_sinks = {"compose-text", "toast-or-snackbar", "ui-text-parameter"}
    if {finding.sink for finding in direct_ui} != expected_sinks:
        failures.append("direct-UI-literal negative control did not fail")

    xml_probe = ElementTree.fromstring(
        '<TextView xmlns:android="http://schemas.android.com/apk/res/android" '
        'android:text="Untranslated XML text" />'
    )
    if not find_android_xml_text(xml_probe, "app/src/main/res/layout/probe.xml"):
        failures.append("Android-XML-literal negative control did not fail")

    duplicate_owner = {
        "default": {
            ("string", "duplicate"): ElementTree.fromstring(
                '<string name="duplicate">Duplicate</string>'
            )
        }
    }
    if not verify_cross_owner_resources(duplicate_owner, duplicate_owner):
        failures.append("cross-owner negative control did not fail")

    pseudo_source = "Long label %1$s"
    pseudo, visible_letters = pseudo_localize(pseudo_source)
    if placeholder_signature(pseudo) != placeholder_signature(pseudo_source):
        failures.append("pseudo-locale placeholder control failed")
    expected_growth = max(4, math.ceil(visible_letters * 0.35))
    if len(pseudo) - len(pseudo_source) - len("⟦ ⟧") < expected_growth:
        failures.append("pseudo-locale expansion control failed")

    if resource_key_prefix_error(
        "shared",
        "default",
        "play_strings.xml",
        "string",
        "temperature_freeze",
    ) is None:
        failures.append("resource-key namespace negative control did not fail")
    return failures


def main() -> None:
    loaded: dict[str, dict[str, dict[tuple[str, str], ElementTree.Element]]] = {}
    errors: list[str] = []
    for owner, root in RESOURCE_ROOTS.items():
        errors.extend(verify_resource_file_layout(owner, root))
        locales, load_errors = load_resources(owner, root)
        loaded[owner] = locales
        errors.extend(load_errors)
        default = locales["default"]
        errors.extend(verify_resource_internals(owner, "default", default))
        for locale in EXPECTED_LOCALES:
            resources = locales[locale]
            errors.extend(verify_resource_internals(owner, locale, resources))
            errors.extend(verify_locale_against_default(owner, locale, resources, default))

    errors.extend(verify_cross_owner_resources(loaded["app"], loaded["shared"]))
    errors.extend(verify_android_bootstrap_resource_boundary(loaded["app"]))
    errors.extend(verify_release_language_selector(loaded["shared"]))
    errors.extend(verify_shared_resource_references(loaded["shared"]))
    errors.extend(verify_no_missing_translation_suppression())

    source_literal_errors, source_literal_count = verify_source_ui_literals()
    errors.extend(source_literal_errors)
    xml_literals = direct_android_xml_text()
    errors.extend(f"direct Android XML text: {finding}" for finding in xml_literals)

    pseudo_errors, pseudo_count = verify_pseudo_locale_expansion(loaded)
    errors.extend(pseudo_errors)
    errors.extend(f"internal gate self-test: {error}" for error in negative_control_errors())
    if errors:
        fail(errors)

    app_count = len(loaded["app"]["default"])
    shared_count = len(loaded["shared"]["default"])
    print(
        "strict locale resources verified: "
        f"{len(EXPECTED_LOCALES)} translations, {app_count} app defaults, "
        f"{shared_count} shared defaults"
    )
    print(
        "localized resource file and key namespace layout verified: "
        f"{len(RESOURCE_FILE_LAYOUT['app'])} app files, "
        f"{len(RESOURCE_FILE_LAYOUT['shared'])} shared files per locale"
    )
    print(
        "UI literal gate verified: "
        f"{source_literal_count} allowlisted source literal, {len(xml_literals)} XML literals"
    )
    print(f"pseudo-locale expansion verified: {pseudo_count} text variants")
    print(
        "negative controls verified: missing/orphan key, placeholder type/order/Compose support, "
        "plural quantities, array shape, resource owner/key namespace, UI/XML literal"
    )


if __name__ == "__main__":
    main()
