#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${REPO_ROOT}"

failed=0

if ! python3 scripts/verify-test-inventory.py; then
    failed=1
fi

if ! python3 scripts/verify-native-format-boundary.py; then
    failed=1
fi

if ! python3 scripts/verify-native-session-boundary.py; then
    failed=1
fi

if ! python3 scripts/verify-native-build-policy.py; then
    failed=1
fi

if ! python3 scripts/verify-vulkan-capability-boundary.py; then
    failed=1
fi

if ! python3 scripts/verify-gene-access-boundary.py; then
    failed=1
fi

for forbidden_path in \
    desktopApp \
    legacy-substrate-converter \
    simulation-core \
    simulation-api \
    simulation-native-bridge \
    simulation-snapshot-model \
    shared/src/desktopMain \
    first_commit.zip \
    first_commit_original; do
    if [[ -e "${forbidden_path}" ]]; then
        echo "Forbidden release path: ${forbidden_path}" >&2
        failed=1
    fi
done

if [[ "$(rg '^include ' settings.gradle)" != "include ':app', ':shared'" ]]; then
    echo "settings.gradle must expose only :app and :shared" >&2
    failed=1
fi

version_catalog=gradle/libs.versions.toml
build_configuration_files=(
    build.gradle
    settings.gradle
    app/build.gradle
    shared/build.gradle
    gradle.properties
)

if [[ ! -f "${version_catalog}" ]]; then
    echo "Gradle version catalog is missing: ${version_catalog}" >&2
    failed=1
else
    for required_catalog_key in \
        agp kotlin composeMultiplatform jdk jvmTarget \
        compileSdk minSdk targetSdk ndk cmake \
        appVersionCode appVersionName; do
        if ! grep -Eq "^${required_catalog_key} = \"[^\"]+\"$" "${version_catalog}"; then
            echo "Missing catalogued build version: ${required_catalog_key}" >&2
            failed=1
        fi
    done
    if ! grep -Fxq 'jdk = "21"' "${version_catalog}"; then
        echo "The catalogued Gradle runtime must remain JDK 21" >&2
        failed=1
    fi
    if ! grep -Fxq 'appVersionCode = "1"' "${version_catalog}" \
        || ! grep -Fxq 'appVersionName = "0.1.0-alpha.1"' "${version_catalog}"; then
        echo "Unexpected Android release version metadata in ${version_catalog}" >&2
        failed=1
    fi
    catalog_versions_section="$(
        sed -n '/^\[versions\]$/,/^\[/p' "${version_catalog}" | sed '1d;$d'
    )"
    if grep -Eiq 'latest\.|SNAPSHOT|[+]' <<<"${catalog_versions_section}"; then
        echo "Dynamic or snapshot version found in ${version_catalog}" >&2
        failed=1
    fi
fi

deprecated_android_property_pattern='android\.(usesSdkInManifest\.disallowed|sdk\.defaultTargetSdkToCompileSdkIfUnset|enableAppCompileTimeRClass|builtInKotlin|newDsl|r8\.optimizedResourceShrinking|defaults\.buildfeatures\.resvalues|uniquePackageNames|nonTransitiveRClass|dependency\.useConstraints|r8\.strictFullModeForKeepRules)='
deprecated_android_property_matches="$(
    rg -n -- "${deprecated_android_property_pattern}" "${build_configuration_files[@]}" || true
)"
if [[ -n "${deprecated_android_property_matches}" ]]; then
    echo "Obsolete or redundant Android Gradle compatibility property found:" >&2
    echo "${deprecated_android_property_matches}" >&2
    failed=1
fi

if ! rg -q '^org\.gradle\.configuration-cache=true$' gradle.properties; then
    echo "Gradle configuration cache must remain enabled" >&2
    failed=1
fi
if ! rg -q 'libs\.versions\.jdk\.get\(\)' build.gradle; then
    echo "The local Gradle runtime must use the catalogued JDK 21" >&2
    failed=1
fi

literal_build_versions="$(
    rg -n \
        -e '[[:alnum:]_.-]+:[[:alnum:]_.-]+:[0-9]' \
        -e 'version[[:space:]]+[\x22\x27][0-9]' \
        "${build_configuration_files[@]}" || true
)"
if [[ -n "${literal_build_versions}" ]]; then
    echo "Plugin or dependency version declared outside ${version_catalog}:" >&2
    echo "${literal_build_versions}" >&2
    failed=1
fi
if rg -q 'buildscript[[:space:]]*\{|resolutionStrategy' "${build_configuration_files[@]}"; then
    echo "Legacy buildscript or global dependency resolution rule found" >&2
    failed=1
fi
if ! rg -q 'RepositoriesMode\.FAIL_ON_PROJECT_REPOS' settings.gradle; then
    echo "Dependency repositories must remain centralized in settings.gradle" >&2
    failed=1
fi

if ! rg -q 'alias\(libs\.plugins\.android\.kotlin\.multiplatform\.library\)' shared/build.gradle \
    || rg -q 'org\.jetbrains\.kotlin\.android|com\.android\.library|androidTarget[[:space:]]*\(' \
        app/build.gradle shared/build.gradle; then
    echo "Android modules must use AGP 9 built-in Kotlin and the Android-KMP plugin" >&2
    failed=1
fi

for alignment_alias in \
    androidx.compose.animation \
    androidx.compose.animation.core \
    androidx.compose.foundation.layout \
    androidx.compose.material.ripple \
    androidx.compose.runtime \
    androidx.lifecycle.runtime \
    androidx.activity \
    androidx.core \
    androidx.core.ktx; do
    if ! grep -Fq "implementation(libs.${alignment_alias})" app/build.gradle; then
        echo "Explicit Android dependency alignment is missing: ${alignment_alias}" >&2
        failed=1
    fi
done

transitional_java_references="$(
    rg -n --glob '*.{java,kt}' \
        'com\.cytarch\.cellularity\.(Gene|Environment|SimulationFacade|SimulationStateSource|SnapshotDecodeException|SubstrateFileCodec)|\b(SimulationModelMapper|NativeSnapshotCodec)\b' \
        app/src/main shared/src || true
)"
if [[ -n "${transitional_java_references}" ]]; then
    echo "Transitional Java simulation reference found:" >&2
    echo "${transitional_java_references}" >&2
    failed=1
fi

for required_license in \
    LICENSE \
    native/third_party/highway/LICENSE \
    native/third_party/highway/LICENSE-BSD3 \
    native/third_party/nlohmann_json/LICENSE.MIT \
    native/third_party/stb/LICENSE \
    native/third_party/zstd/LICENSE \
    third_party/nunito/OFL.txt; do
    if [[ ! -f "${required_license}" ]]; then
        echo "Required license file is missing: ${required_license}" >&2
        failed=1
    fi
done

for required_release_document in CHANGELOG.md RELEASE_CHECKLIST.md; do
    if [[ ! -f "${required_release_document}" ]]; then
        echo "Required release document is missing: ${required_release_document}" >&2
        failed=1
    fi
done

legacy_ui_model_import_pattern='^import com\.cytarch\.cellularity\.(CellData|CellDataConstants|CellSnapshot|CellType|EnvironmentRevisionState|EnvironmentState|Link|LinkConstants|ScalarFieldType|SimulationStatsSnapshot|VectorFieldType);?$'
legacy_ui_model_imports="$(
    rg -n --glob '*.{java,kt}' -- "${legacy_ui_model_import_pattern}" \
        app/src/main/java/com/cytarch/cellularity/ui \
        shared/src/commonMain || true
)"
if [[ -n "${legacy_ui_model_imports}" ]]; then
    echo "UI imports transitional Java snapshot, enum, or constants models:" >&2
    echo "${legacy_ui_model_imports}" >&2
    failed=1
fi

legacy_ui_runtime_import_pattern='^import com\.cytarch\.cellularity\.(EnvironmentEditor|NativeSimulationFacade|SimulationFacade|SimulationStateSource);?$'
legacy_ui_runtime_imports="$(
    rg -n --glob '*.{java,kt}' -- "${legacy_ui_runtime_import_pattern}" \
        app/src/main/java/com/cytarch/cellularity/ui \
        shared/src/commonMain || true
)"
if [[ -n "${legacy_ui_runtime_imports}" ]]; then
    echo "UI imports a transitional native simulation runtime contract:" >&2
    echo "${legacy_ui_runtime_imports}" >&2
    failed=1
fi

ui_render_backend_imports="$(
    rg -n --glob '*.{java,kt}' \
        '^import com\.cytarch\.cellularity\.platform\.simulation\.MicroscopeSimulationBackend;?$' \
        app/src/main/java/com/cytarch/cellularity/ui || true
)"
if [[ -n "${ui_render_backend_imports}" ]]; then
    echo "UI must receive a surface factory, not low-level simulation renderer methods:" >&2
    echo "${ui_render_backend_imports}" >&2
    failed=1
fi

common_gateway_platform_imports="$(
    rg -n --glob '*.{java,kt}' \
        '^import (android\.|androidx\.(activity|core|lifecycle|preference)|com\.cytarch\.cellularity\.(Native|SimulationFacade|EnvironmentEditor))' \
        shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/simulation || true
)"
if [[ -n "${common_gateway_platform_imports}" ]]; then
    echo "Common simulation gateway exposes an Android, JNI, or transitional runtime dependency:" >&2
    echo "${common_gateway_platform_imports}" >&2
    failed=1
fi

session_contract=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/simulation/SimulationGateway.kt
for required_state in New Opening Ready Paused Saving Closing Closed Failed; do
    if ! rg -q "data (object|class) ${required_state}[ (:]" "${session_contract}"; then
        echo "Simulation session state is missing: ${required_state}" >&2
        failed=1
    fi
done

session_gateway=app/src/main/java/com/cytarch/cellularity/platform/simulation/AndroidSimulationGateway.kt
session_attachment=app/src/main/java/com/cytarch/cellularity/platform/simulation/SessionMicroscopeRenderEndpoint.kt
retained_owner=app/src/main/java/com/cytarch/cellularity/platform/simulation/RetainedSimulationOwner.kt
play_view_model=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayActivityViewModel.kt
if ! rg -q 'Channel<QueuedSessionAction>|ReentrantLock' "${session_gateway}" \
    || ! rg -q 'activeSurfaceToken|surfaceToken' "${session_gateway}" "${session_attachment}" \
    || ! rg -q 'RetainedSimulationOwner' "${retained_owner}" "${play_view_model}"; then
    echo "Simulation lifecycle must retain one owner, one serial queue, and tokenized render attachments" >&2
    failed=1
fi
if rg -q 'private var simulationConnection' "${play_view_model}"; then
    echo "Play ViewModel must use the retained simulation owner during Activity recreation" >&2
    failed=1
fi
android_file_repository=app/src/main/java/com/cytarch/cellularity/platform/files/AndroidCellularityFileRepository.kt
if ! rg -q 'context\.applicationContext \?: context' "${android_file_repository}"; then
    echo "A retained simulation repository must not retain an Activity context" >&2
    failed=1
fi

whole_substrate_heap_apis="$(
    rg -n --glob '*.{java,kt,cpp,h}' \
        'readSubstrate.*ByteArray|writeSubstrate.*ByteArray|substrateDataSnapshot|nativeCreateFromSubstrate(Bytes|Data)' \
        app/src/main shared/src/commonMain native/jni || true
)"
if [[ -n "${whole_substrate_heap_apis}" ]]; then
    echo "Whole-file substrate heap API found:" >&2
    echo "${whole_substrate_heap_apis}" >&2
    failed=1
fi
if rg -q 'SubstrateFileCodec' app/src/main shared/src; then
    echo "Production Android/shared storage must use native substrate inspection" >&2
    failed=1
fi

substrate_info_sources=(
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/substrate_info
    app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/substrate_info
)
if rg -q '\bstateTick\b' "${substrate_info_sources[@]}"; then
    echo "Substrate Info must update from immutable snapshots without stateTick" >&2
    failed=1
fi

if ! rg -q 'saveSubstrateAtomically' \
    "${android_file_repository}" \
    app/src/main/java/com/cytarch/cellularity/platform/simulation/NativeSimulationRuntime.kt; then
    echo "Native substrate save must use the atomic private-file transaction" >&2
    failed=1
fi

common_model_behavior_pattern='ObjectInputStream|ObjectOutputStream|kotlinx\.serialization|fun[[:space:]]+(mutate|update|readFrom|writeTo)[[:space:]]*\('
common_model_behavior="$(
    rg -n -- "${common_model_behavior_pattern}" \
        shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/model || true
)"
if [[ -n "${common_model_behavior}" ]]; then
    echo "Common DTOs must not contain engine mutation or serialization behavior:" >&2
    echo "${common_model_behavior}" >&2
    failed=1
fi

obsolete_cyrillic_identifier=$'\u0421\u044d\u0442\u0435\u0440\u0441\u043a\u043e\u0433'
old_identifier_pattern='Pett''er|Sater''skog|Säter''skog|'"${obsolete_cyrillic_identifier}"'|Cell[[:space:]_-]?''Lab|cell''lab|Min[[:space:]_-]?''Lab|com[./]a''cl'
old_identifier_matches="$(
    rg -n -i --hidden \
        --glob '!.git' \
        --glob '!.git/**' \
        --glob '!**/build/**' \
        --glob '!**/.cxx/**' \
        --glob '!scripts/check-public-release.sh' \
        -- "${old_identifier_pattern}" . || true
)"
if [[ -n "${old_identifier_matches}" ]]; then
    echo "Obsolete identifiers found:" >&2
    echo "${old_identifier_matches}" >&2
    failed=1
fi

obsolete_path_matches="$(
    if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git ls-files | grep -E -i -- "${old_identifier_pattern}" || true
    else
        find . \
            \( -path './.git' -o -path './.codex' -o -path './.gradle' \
               -o -path './.kotlin' -o -path './.idea' \
               -o -path '*/build' -o -path '*/.cxx' \) -prune \
            -o -print \
            | grep -E -i -- "${old_identifier_pattern}" || true
    fi
)"
if [[ -n "${obsolete_path_matches}" ]]; then
    echo "Obsolete identifiers found in paths:" >&2
    echo "${obsolete_path_matches}" >&2
    failed=1
fi

unexpected_cyrillic_matches="$(
    rg -n -P --hidden \
        --glob '!.git' \
        --glob '!.git/**' \
        --glob '!.codex/**' \
        --glob '!.gradle/**' \
        --glob '!.kotlin/**' \
        --glob '!.idea/**' \
        --glob '!**/build/**' \
        --glob '!**/.cxx/**' \
        --glob '!**/values-ru/**' \
        --glob '!**/composeResources/values-ru/**' \
        --glob '!shared/src/commonMain/composeResources/values/strings_untranslated.xml' \
        -- '[\x{0400}-\x{04ff}]' . || true
)"
if [[ -n "${unexpected_cyrillic_matches}" ]]; then
    echo "Cyrillic text found outside Russian localization resources:" >&2
    echo "${unexpected_cyrillic_matches}" >&2
    failed=1
fi

language_names_resource=shared/src/commonMain/composeResources/values/strings_untranslated.xml
russian_language_self_name=$'\u0420\u0443\u0441\u0441\u043a\u0438\u0439'
expected_russian_language_line="        <item>${russian_language_self_name}</item>"
actual_language_metadata_cyrillic="$(
    rg -N -P -- '[\x{0400}-\x{04ff}]' "${language_names_resource}" || true
)"
if [[ "${actual_language_metadata_cyrillic}" != "${expected_russian_language_line}" ]]; then
    echo "The language list must contain only the expected Russian self-name in Cyrillic" >&2
    failed=1
fi

legacy_prefixed_declarations="$(
    rg -n \
        --glob '*.{java,kt,cpp,h}' \
        --glob '!native/third_party/**' \
        '\b(class|struct|enum|interface|object|namespace)[[:space:]]+Z[A-Z][A-Za-z0-9_]*\b' \
        app/src shared/src native || true
)"
if [[ -n "${legacy_prefixed_declarations}" ]]; then
    echo "Legacy Z-prefixed source declarations found:" >&2
    echo "${legacy_prefixed_declarations}" >&2
    failed=1
fi

legacy_prefixed_paths="$(
    find app/src shared/src native \
        -path 'native/third_party' -prune -o \
        \( -type f -o -type d \) -print \
        | grep -E '(^|/)(zutil|Z[A-Z][A-Za-z0-9_.-]*)(/|$)' || true
)"
if [[ -n "${legacy_prefixed_paths}" ]]; then
    echo "Legacy Z-prefixed source paths found:" >&2
    echo "${legacy_prefixed_paths}" >&2
    failed=1
fi

secret_pattern='-----BEGIN (RSA|EC|OPENSSH|DSA) PRIVATE KEY-----|A''KIA[0-9A-Z]{16}|A''Iza[0-9A-Za-z_-]{30,}|s''k-[0-9A-Za-z_-]{20,}'
secret_matches="$(
    rg -n --hidden \
        --glob '!.git' \
        --glob '!.git/**' \
        --glob '!**/build/**' \
        --glob '!**/.cxx/**' \
        --glob '!scripts/check-public-release.sh' \
        -- "${secret_pattern}" . || true
)"
if [[ -n "${secret_matches}" ]]; then
    echo "Possible credential material found:" >&2
    echo "${secret_matches}" >&2
    failed=1
fi

sensitive_files="$(
    find . -path './.git' -prune -o -type f \
        \( -iname '*.jks' -o -iname '*.keystore' -o -iname '*.p12' -o -iname '*.pfx' -o -iname '*.pem' \) \
        -print || true
)"
if [[ -n "${sensitive_files}" ]]; then
    echo "Signing or key files found:" >&2
    echo "${sensitive_files}" >&2
    failed=1
fi

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    tracked_artifacts="$(
        git ls-files \
            | grep -E '(^|/)(build|\.gradle|\.cxx|\.externalNativeBuild)(/|$)|\.(apk|aab|apks|idsig|log|hprof)$' || true
    )"
    if [[ -n "${tracked_artifacts}" ]]; then
        echo "Generated artifacts are tracked:" >&2
        echo "${tracked_artifacts}" >&2
        failed=1
    fi
fi

if ! rg -q 'applicationId[[:space:]]*=[[:space:]]*"com\.cytarch\.cellularity"' app/build.gradle; then
    echo "Unexpected Android application ID" >&2
    failed=1
fi

if ! rg -q "namespace[[:space:]]*=[[:space:]]*'com\.cytarch\.cellularity'" app/build.gradle; then
    echo "Unexpected Android namespace" >&2
    failed=1
fi

legacy_package_references="$(
    rg -n --hidden \
        --glob '!.git' \
        --glob '!.git/**' \
        --glob '!.gradle/**' \
        --glob '!.idea/**' \
        --glob '!.kotlin/**' \
        --glob '!.codex/**' \
        --glob '!build/**' \
        --glob '!**/build/**' \
        --glob '!scripts/check-public-release.sh' \
        'com[.]cellularity|com\\[.]cellularity|com/cellularity|com_cellularity' \
        . || true
)"
if [[ -n "${legacy_package_references}" ]]; then
    echo "Legacy com.cellularity package reference found:" >&2
    echo "${legacy_package_references}" >&2
    failed=1
fi

if ! rg -q 'versionCode[[:space:]]*=[[:space:]]*libs\.versions\.appVersionCode\.get\(\)\.toInteger\(\)' app/build.gradle \
    || ! rg -q 'versionName[[:space:]]*=[[:space:]]*libs\.versions\.appVersionName\.get\(\)' app/build.gradle; then
    echo "Unexpected Android release version metadata" >&2
    failed=1
fi

if rg -q 'android:largeHeap=' app/src/main/AndroidManifest.xml; then
    echo "android:largeHeap must not be enabled" >&2
    failed=1
fi

absolute_substrate_limit_owners="$(
    rg -l '2ULL[[:space:]]*\*[[:space:]]*1024ULL[[:space:]]*\*[[:space:]]*1024ULL[[:space:]]*\*[[:space:]]*1024ULL' \
        native app/src/main shared/src \
        --glob '!native/tests/**' \
        --glob '!native/third_party/**' \
        --glob '!**/build/**' || true
)"
if [[ "${absolute_substrate_limit_owners}" != "native/formats/FormatLimits.h" ]]; then
    echo "The absolute 2 GiB substrate limit must have one production owner" >&2
    failed=1
fi
memory_policy=native/core/SimulationMemoryPolicy.cpp
simulation_world=native/core/SimulationWorld.cpp
native_bridge=app/src/main/java/com/cytarch/cellularity/NativeCellularityControllerBridge.java
android_file_repository=app/src/main/java/com/cytarch/cellularity/platform/files/AndroidCellularityFileRepository.kt
if ! rg -q 'runtimeLimitsForDevice' "${memory_policy}" \
    || ! rg -q 'kPhysicalMemoryBudgetDivisor' "${memory_policy}" \
    || ! rg -q 'estimateCurrentWorldAllocation' "${memory_policy}" \
    || ! rg -q 'validatedAllocationEnvironment' "${simulation_world}"; then
    echo "Native allocation policy must validate ABI, device RAM, and configuration before allocation" >&2
    failed=1
fi
if ! rg -q 'nativeRuntimeLimits' "${native_bridge}" \
    || ! rg -q 'nativeEstimateWorldAllocationBytes' "${native_bridge}" \
    || ! rg -q 'operationalSubstrateFileLimitBytes' "${android_file_repository}" \
    || rg -q 'MAX_SUBSTRATE_SIZE_BYTES' app/src/main shared/src; then
    echo "Android and shared layers diverge from the native memory policy" >&2
    failed=1
fi

settings_contract=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/settings/CellularitySettings.kt
if ! rg -q 'val settings: StateFlow<CellularitySettings>' "${settings_contract}" \
    || ! rg -q 'suspend fun initialize\(\): SettingsResult<CellularitySettings>' "${settings_contract}"; then
    echo "Settings repository must publish one initialized typed StateFlow" >&2
    failed=1
fi
if rg -q '\breadSettings\(' app/src/main shared/src/commonMain shared/src/androidMain; then
    echo "Production settings must not expose synchronous disk reads" >&2
    failed=1
fi
settings_owner_leaks="$(
    rg -n 'Android(Cellularity|Display)SettingsRepository\(' app/src/main/java \
        | grep -v '^app/src/main/java/com/cytarch/cellularity/CellularityApplication.kt:' || true
)"
if [[ -n "${settings_owner_leaks}" ]]; then
    echo "Settings repository must have one process owner in CellularityApplication:" >&2
    echo "${settings_owner_leaks}" >&2
    failed=1
fi
application_on_create="$(
    sed -n '/override fun onCreate()/,/^    }/p' \
        app/src/main/java/com/cytarch/cellularity/CellularityApplication.kt
)"
if grep -Eq 'settingsRepository|displaySettingsRepository|initializeSettings' \
    <<<"${application_on_create}"; then
    echo "Application.onCreate must leave settings untouched until the recovery gate" >&2
    failed=1
fi
if ! rg -Uq 'listPendingRecoveryReports[[:space:][:print:]]{0,700}initializeSettings\(\)' \
    app/src/main/java/com/cytarch/cellularity/ui/RecoverableAppCompatActivity.kt; then
    echo "Normal Activity startup must check crash recovery before settings initialization" >&2
    failed=1
fi

store_contract=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/state/Store.kt
feature_store_sources=(
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/settings/SettingsStore.kt
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/freezer/FreezerStore.kt
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/gene_bank/GeneBankStore.kt
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/MainMenuStore.kt
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/PlayStore.kt
)
if ! rg -q 'interface Store<out State, in Action, out Effect>' "${store_contract}"; then
    echo "The common feature Store contract is missing" >&2
    failed=1
fi
for feature_store in "${feature_store_sources[@]}"; do
    if ! rg -q ': Store<' "${feature_store}" \
        || ! rg -q 'Channel<.*>\(Channel\.UNLIMITED\)' "${feature_store}" \
        || ! rg -q 'receiveAsFlow\(\)' "${feature_store}" \
        || ! rg -q 'override suspend fun dispatch\(' "${feature_store}"; then
        echo "Feature store must expose state/action/single-consumer-effect mechanics: ${feature_store}" >&2
        failed=1
    fi
done
feature_store_platform_references="$(
    rg -n '^import (android\.|androidx\.)|\b(Context|Uri|Intent|View|GLSurfaceView|NativeSimulationFacade)\b' \
        "${store_contract}" "${feature_store_sources[@]}" || true
)"
if [[ -n "${feature_store_platform_references}" ]]; then
    echo "Common feature state/actions/effects contain platform objects:" >&2
    echo "${feature_store_platform_references}" >&2
    failed=1
fi
if rg -q 'restartRequested|launchInProgress' \
    app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/SettingsScreen.kt \
    app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/experiments/FreezerScreen.kt; then
    echo "One-shot feature events must not be represented as local or persistent UI flags" >&2
    failed=1
fi

play_activity=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayActivity.kt
play_view_model=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayActivityViewModel.kt
play_content=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayContent.kt
play_launch_parser=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlayLaunchParser.kt
play_session_controller=app/src/main/java/com/cytarch/cellularity/ui/play_menu/PlaySessionController.kt
play_screen=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/PlayScreen.kt
play_store=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/PlayStore.kt
if ! rg -q 'class PlayStore' "${play_store}" \
    || ! rg -q 'state: PlayState' "${play_screen}" \
    || ! rg -q 'onAction: \(PlayAction\) -> Unit' "${play_screen}" \
    || ! rg -q 'class PlaySessionController' "${play_session_controller}" \
    || ! rg -q 'object PlayLaunchParser' "${play_launch_parser}"; then
    echo "Play must keep separate common store/UI, launch codec, and retained session owner" >&2
    failed=1
fi
play_activity_domain_roles="$(
    rg -n 'class PlayActivity[^\n]*(MicroscopeCellSource|GenomeProvider)|^import .*\.(MicroscopeCellSource|GenomeProvider)$' \
        "${play_activity}" || true
)"
if [[ -n "${play_activity_domain_roles}" ]]; then
    echo "PlayActivity must not implement or import a simulation/editor domain provider:" >&2
    echo "${play_activity_domain_roles}" >&2
    failed=1
fi
play_state_duplicates="$(
    rg -n '\b(MutableLiveData|LiveData|MutableStateFlow)\b|\b(mUri|configuration|old|external)\s*[:=]' \
        "${play_activity}" "${play_view_model}" || true
)"
if [[ -n "${play_state_duplicates}" ]]; then
    echo "Play Activity/ViewModel must not duplicate retained Play state:" >&2
    echo "${play_state_duplicates}" >&2
    failed=1
fi
play_platform_leaks="$(
    rg -n '^import android\.|^import androidx\.(activity|core|lifecycle|preference)|\b(PlayActivity|Context|Intent|Uri|Bundle|View|GLSurfaceView)\b' \
        "${play_store}" "${play_screen}" || true
)"
if [[ -n "${play_platform_leaks}" ]]; then
    echo "Common Play state and UI contain Android platform objects:" >&2
    echo "${play_platform_leaks}" >&2
    failed=1
fi
play_adapter_product_actions="$(
    rg -n '\b(StorageErrorCode|StorageResult|SimulationCommand|PlaySessionOperations)\b|fileRepository\.(substrateExists|inspectStoredSubstrate|saveSubstrate)|gateway\.(save|execute)\(' \
        "${play_activity}" "${play_content}" || true
)"
if [[ -n "${play_adapter_product_actions}" ]]; then
    echo "Play Android composition/window adapters still execute product or file actions:" >&2
    echo "${play_adapter_product_actions}" >&2
    failed=1
fi
if rg -q '\b(CellularityAlertDialog|AlertDialog)\b|\bDialog\(' \
    "${play_activity}" "${play_content}" \
    || ! rg -q '\bCellularityAlertDialog\(' "${play_screen}"; then
    echo "Play dialogs must remain controlled by the shared Play UI" >&2
    failed=1
fi
direct_compose_dialog_imports="$(
    rg -n --glob '*.kt' --glob '!CellularityAlertDialog.kt' \
        '^import (androidx\.compose\.material3\.(AlertDialog|BasicAlertDialog)|androidx\.compose\.ui\.window\.Dialog)$' \
        app/src/main/java shared/src/commonMain/kotlin || true
)"
if [[ -n "${direct_compose_dialog_imports}" ]]; then
    echo "Compose dialogs must use the shared CellularityAlertDialog style:" >&2
    echo "${direct_compose_dialog_imports}" >&2
    failed=1
fi
if rg -q 'putParcelable|putSerializable|\b(Uri|InputStream|OutputStream|ParcelFileDescriptor)\b' \
    "${play_launch_parser}" \
    || ! rg -q 'PlayPersistenceState' "${play_launch_parser}" \
    || ! rg -q 'put(Int|String|Double)\(' "${play_launch_parser}"; then
    echo "Play saved state must contain only primitive state and serializable-by-value IDs" >&2
    failed=1
fi
if ! rg -q 'SimulationContentState' \
    shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/simulation/SimulationGateway.kt \
    "${play_store}" \
    || ! rg -q 'pendingContentChange' "${play_store}"; then
    echo "Play dirty state must remain gateway-owned and command-aware" >&2
    failed=1
fi

microscope_common_dir=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/play/microscope
microscope_state="${microscope_common_dir}/MicroscopeState.kt"
microscope_input="${microscope_common_dir}/MicroscopeInput.kt"
microscope_diagnosis="${microscope_common_dir}/MicroscopeDiagnosisMapper.kt"
microscope_screen="${microscope_common_dir}/MicroscopeScreen.kt"
microscope_android_screen=app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeScreen.kt
microscope_android_view=app/src/main/java/com/cytarch/cellularity/platform/render/microscope/AndroidMicroscopeView.kt
microscope_render_contract=app/src/main/java/com/cytarch/cellularity/platform/render/microscope/MicroscopeRenderEndpoint.kt
microscope_gles_surface=app/src/main/java/com/cytarch/cellularity/platform/render/microscope/GlesMicroscopeSurfaceView.kt
for obsolete_microscope_wrapper in \
    app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeFragment.kt \
    app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeSurfaceView.java \
    app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeView.kt \
    app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/ToolType.kt; do
    if [[ -e "${obsolete_microscope_wrapper}" ]]; then
        echo "Obsolete microscope wrapper remains: ${obsolete_microscope_wrapper}" >&2
        failed=1
    fi
done
if ! rg -q 'data class MicroscopeState' "${microscope_state}" \
    || ! rg -q 'class MicroscopeStore' "${microscope_state}" \
    || ! rg -q 'onAction: \(MicroscopeAction\) -> Unit' "${microscope_screen}" \
    || ! rg -q 'private val microscopeStore = MicroscopeStore\(\)' "${play_view_model}" \
    || rg -q 'remember[[:space:]]*\{[[:space:]]*MicroscopeState\(' "${play_content}"; then
    echo "Microscope must use one retained immutable common state/action store" >&2
    failed=1
fi
if rg -q 'mutable(Int)?StateOf|var (selectedTool|selectedTemperature|dialog|zoomLabel|simulationSpeedLabel|diagnosis)' \
    "${microscope_state}"; then
    echo "Microscope state must not expose Compose or mutable feature fields" >&2
    failed=1
fi
if ! rg -q 'class MicroscopeGestureInterpreter' "${microscope_input}" \
    || ! rg -q 'MotionEventMicroscopePointerEventView' "${microscope_android_view}" \
    || ! rg -q 'gestureInterpreter\.onPointerEvent' "${microscope_android_view}" \
    || ! rg -q 'endpoint\.submitMicroscopeInputBatch' "${microscope_android_view}" \
    || ! rg -q 'interface MicroscopeRenderEndpoint' "${microscope_render_contract}" \
    || rg -q 'microscopeRenderer\.(moveView|zoomView|touchDown|touchMove|touchUp|onSingleTapUp|onFling)' \
        "${microscope_android_view}"; then
    echo "Microscope MotionEvents must cross the renderer boundary as typed commands" >&2
    failed=1
fi
if ! rg -q 'object MicroscopeDiagnosisMapper' "${microscope_diagnosis}" \
    || ! rg -q 'CellInspectionSnapshot' "${microscope_diagnosis}" \
    || [[ -e app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope/MicroscopeState.kt ]] \
    || rg -q '\b(Context|HtmlCompat|Spanned)\b|R\.string' \
        "${microscope_diagnosis}" "${microscope_android_screen}"; then
    echo "Microscope diagnosis must use shared snapshots and shared resource text tokens" >&2
    failed=1
fi
if rg -q 'name="(cell_info_|signal_substance_unit|value_true|value_false)' \
    app/src/main/res/values* --glob '*.xml'; then
    echo "Shared microscope diagnosis resources must not be duplicated as unused app resources" >&2
    failed=1
fi
if rg -q '\bGLSurfaceView\b|^import android\.' "${microscope_input}" "${microscope_state}" \
    "${microscope_diagnosis}" \
    || ! rg -q 'DefaultLifecycleObserver|MicroscopeRenderSurfaceHostFactory' \
        "${microscope_android_view}" \
    || ! rg -q 'GLSurfaceView' "${microscope_gles_surface}"; then
    echo "Microscope surface lifecycle must remain in the Android adapter" >&2
    failed=1
fi
microscope_legacy_runtime_references="$(
    rg -n '^import com\.cytarch\.cellularity\.(Environment|Gene|SimulationWorld);?$' \
        "${microscope_common_dir}" \
        app/src/main/java/com/cytarch/cellularity/ui/play_menu/screens/microscope \
        app/src/main/java/com/cytarch/cellularity/platform/render/microscope || true
)"
if [[ -n "${microscope_legacy_runtime_references}" ]]; then
    echo "Microscope imports a removed Java simulation model:" >&2
    echo "${microscope_legacy_runtime_references}" >&2
    failed=1
fi

freezer_adapter=app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/experiments/FreezerScreen.kt
gene_bank_adapter=app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/gene_bank/GeneBankScreen.kt
file_collection_adapters=("${freezer_adapter}" "${gene_bank_adapter}")
file_repository_contract=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/files/CellularityFileRepository.kt
freezer_screen=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/freezer/FreezerScreen.kt
gene_bank_store=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/gene_bank/GeneBankStore.kt
file_sharer=app/src/main/java/com/cytarch/cellularity/platform/files/AndroidStoredFileSharer.kt
if [[ -e shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/gene_bank/GeneBankState.kt ]] \
    || rg -q '\bFreezerTexts\b' app/src/main shared/src/commonMain; then
    echo "Obsolete synchronous/text-fed Freezer or Gene Bank state remains" >&2
    failed=1
fi
file_collection_product_branches="$(
    rg -n '\b(StorageErrorCode|StorageResult|SimulationContract|CellularityFileNames|FreezerTexts)\b|shareUriForInternalFile' \
        "${file_collection_adapters[@]}" || true
)"
if [[ -n "${file_collection_product_branches}" ]]; then
    echo "Freezer/Gene Bank Android adapters still contain product or storage branching:" >&2
    echo "${file_collection_product_branches}" >&2
    failed=1
fi
if ! rg -q 'StoredSubstrateId' "${file_repository_contract}" \
    || ! rg -q 'StoredGenomeId' "${file_repository_contract}" \
    || ! rg -q 'listSubstrateIds.*StoredSubstrateId' "${file_repository_contract}" \
    || ! rg -q 'listGenomeIds.*StoredGenomeId' "${file_repository_contract}"; then
    echo "Stored substrate/genome IDs must remain distinct from display names" >&2
    failed=1
fi
if ! rg -q 'estimateMemoryBytes: \(Int, Int, Double\) -> Long\?' "${freezer_screen}" \
    || ! rg -q 'NativeCellularityControllerBridge\.estimateWorldAllocationBytes' "${freezer_adapter}" \
    || rg -q 'SimulationContract\.estimateMemoryBytes|estimateUsageText' \
        "${freezer_screen}" "${freezer_adapter}"; then
    echo "Freezer must keep a shared capability boundary with native-owned memory policy" >&2
    failed=1
fi
if ! rg -q 'class GeneBankStore' "${gene_bank_store}" \
    || ! rg -q 'AndroidStoredFileSharer' "${file_sharer}" "${file_collection_adapters[@]}" \
    || ! rg -q 'ShareFinished' \
        shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/freezer/FreezerStore.kt \
        "${gene_bank_store}"; then
    echo "Freezer/Gene Bank action/effect and Android share boundaries are incomplete" >&2
    failed=1
fi

main_menu_activity=app/src/main/java/com/cytarch/cellularity/ui/main_menu/MainMenuActivity.kt
main_menu_view_model=app/src/main/java/com/cytarch/cellularity/ui/main_menu/MainMenuViewModel.kt
main_menu_import_coordinator=app/src/main/java/com/cytarch/cellularity/ui/main_menu/MainMenuImportCoordinator.kt
main_menu_screen=shared/src/commonMain/kotlin/com/cytarch/cellularity/shared/ui/main_menu/MainMenuScreen.kt
recovery_activity=app/src/main/java/com/cytarch/cellularity/ui/recovery/RecoveryActivity.kt
if rg -q 'mutable(Int)?StateOf|handleExternalIntent|AndroidCellularityFileRepository|resolveExternalFile|importExternalFile|incomingIntent\.data[[:space:]]*=' \
    "${main_menu_activity}"; then
    echo "MainMenuActivity must not own mutable menu state or external import transport" >&2
    failed=1
fi
if ! rg -q 'MainMenuViewModel by viewModels' "${main_menu_activity}" \
    || ! rg -q 'settingsRepository\.settings\.collectAsStateWithLifecycle' "${main_menu_activity}" \
    || ! rg -q 'LaunchedEffect\(settings\.language\)' "${main_menu_activity}" \
    || ! rg -q 'override fun onNewIntent' "${main_menu_activity}" \
    || ! rg -q 'STATE_COMPLETED_IMPORT_COMMAND_IDS' "${main_menu_activity}" "${main_menu_view_model}"; then
    echo "Main menu must observe settings and use a saved, idempotent Intent command" >&2
    failed=1
fi
if ! rg -q 'class AndroidMainMenuImportCoordinator' "${main_menu_import_coordinator}" \
    || ! rg -q 'EXTRA_IMPORT_COMMAND_ID' "${main_menu_import_coordinator}" \
    || ! rg -q 'resolveExternalFile|importExternalFile' "${main_menu_import_coordinator}" \
    || ! rg -q 'MainMenuImportOrchestrator' "${main_menu_view_model}"; then
    echo "External file handling must remain in the retained Android import coordinator" >&2
    failed=1
fi
if ! rg -q 'MainMenuState' "${main_menu_screen}" \
    || ! rg -q 'state\.importInProgress' "${main_menu_screen}" \
    || ! rg -q 'state\.collectionRevision' "${main_menu_screen}" \
    || ! rg -q 'externalRefreshRevision' \
        app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/experiments/FreezerScreen.kt \
        app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/gene_bank/GeneBankScreen.kt \
    || rg -q 'setApplicationLanguage' \
        app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/SettingsScreen.kt; then
    echo "Shared Main Menu state must own page/progress while its Activity owns locale integration" >&2
    failed=1
fi
if ! rg -Uq 'createForwardIntent\(this, intent, MAIN_ACTIVITY_CLASS_NAME\)\.addFlags\([[:space:]]*Intent\.FLAG_ACTIVITY_CLEAR_TOP or Intent\.FLAG_ACTIVITY_SINGLE_TOP' \
    "${recovery_activity}"; then
    echo "Recovery must deliver repeated external imports to MainMenuActivity.onNewIntent" >&2
    failed=1
fi

if ! rg -Uq 'android:name="com\.cytarch\.cellularity\.ui\.play_menu\.PlayActivity"[[:space:][:print:]]{0,300}android:exported="false"' \
    app/src/main/AndroidManifest.xml; then
    echo "PlayActivity must remain private" >&2
    failed=1
fi

if rg -q '<files-path|<external-path|<external-files-path' app/src/main/res/xml/file_paths.xml \
    || ! rg -q '<cache-path name="exports" path="exports/"' app/src/main/res/xml/file_paths.xml; then
    echo "FileProvider must expose only the cache exports directory" >&2
    failed=1
fi

diagnostics_implementation=(
    app/src/main/java/com/cytarch/cellularity/CrashLogger.kt
    app/src/main/java/com/cytarch/cellularity/CrashReportStore.kt
    app/src/main/java/com/cytarch/cellularity/DiagnosticsShare.kt
    app/src/main/java/com/cytarch/cellularity/ui/recovery/RecoveryActivity.kt
    app/src/main/java/com/cytarch/cellularity/ui/main_menu/screens/settings/SettingsScreen.kt
    native/diagnostics/NativeCrashHandler.cpp
    native/diagnostics/NativeCrashReportWriter.cpp
)
if rg -q 'MediaStore|DIRECTORY_DOWNLOADS|getExternalStoragePublicDirectory|ClipboardManager|setPrimaryClip|__android_log_write' \
    "${diagnostics_implementation[@]}"; then
    echo "Diagnostics must stay in app-private storage until explicit export" >&2
    failed=1
fi
if ! rg -q 'noBackupFilesDir' app/src/main/java/com/cytarch/cellularity/CrashLogger.kt \
    || ! rg -q 'cacheDir' app/src/main/java/com/cytarch/cellularity/CrashLogger.kt \
    || ! rg -q 'createDiagnosticsExport' app/src/main/java/com/cytarch/cellularity/DiagnosticsShare.kt \
    || ! rg -q 'System\.loadLibrary\("cellularity_diagnostics"\)' \
        app/src/main/java/com/cytarch/cellularity/CrashLogger.kt; then
    echo "Unexpected diagnostics storage or export path" >&2
    failed=1
fi

if rg -q 'System\.loadLibrary\("cellularity"\)' \
    app/src/main/java/com/cytarch/cellularity/CrashLogger.kt \
    app/src/main/java/com/cytarch/cellularity/ui/recovery/RecoveryActivity.kt; then
    echo "Recovery startup must not load the simulation library" >&2
    failed=1
fi
if rg -q 'androidx\.compose|com\.cytarch\.cellularity\.shared|NativeSimulation|SimulationWorld|Environment' \
    app/src/main/java/com/cytarch/cellularity/ui/recovery/RecoveryActivity.kt; then
    echo "RecoveryActivity must remain independent of app UI and simulation runtime" >&2
    failed=1
fi

recovery_activity_block="$(
    sed -n '/android:name="com.cytarch.cellularity.ui.recovery.RecoveryActivity"/,/<\/activity>/p' \
        app/src/main/AndroidManifest.xml
)"
if ! grep -q 'android:exported="true"' <<<"${recovery_activity_block}" \
    || ! grep -q 'android.intent.action.MAIN' <<<"${recovery_activity_block}" \
    || ! grep -q 'android.intent.category.LAUNCHER' <<<"${recovery_activity_block}" \
    || ! grep -q 'android.intent.action.VIEW' <<<"${recovery_activity_block}"; then
    echo "RecoveryActivity must own launcher and external import entry points" >&2
    failed=1
fi

main_activity_block="$(
    sed -n '/android:name="com.cytarch.cellularity.ui.main_menu.MainMenuActivity"/,/<activity/p' \
        app/src/main/AndroidManifest.xml | head -n 6
)"
if ! grep -q 'android:exported="false"' <<<"${main_activity_block}"; then
    echo "MainMenuActivity must remain behind RecoveryActivity" >&2
    failed=1
fi

app_name_resource=app/src/main/res/values/strings_untranslated.xml
if ! rg -q '<string name="app_name" translatable="false">Cellularity</string>' \
    "${app_name_resource}"; then
    echo "Unexpected application display name in ${app_name_resource}" >&2
    failed=1
fi
if rg -q '<string name="app_name"' shared/src/commonMain/composeResources/values; then
    echo "Platform application name must not be duplicated in shared resources" >&2
    failed=1
fi

expected_wrapper_sha256='76805e32c009c0cf0dd5d206bddc9fb22ea42e84db904b764f3047de095493f3'
actual_wrapper_sha256="$(sha256sum gradle/wrapper/gradle-wrapper.jar | awk '{print $1}')"
if [[ "${actual_wrapper_sha256}" != "${expected_wrapper_sha256}" ]]; then
    echo "Unexpected Gradle 9.1.0 wrapper JAR checksum: ${actual_wrapper_sha256}" >&2
    failed=1
fi

if [[ "${failed}" -ne 0 ]]; then
    exit 1
fi

echo "Public release tree checks passed."
