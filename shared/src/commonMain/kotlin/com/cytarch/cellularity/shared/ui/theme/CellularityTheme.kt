package com.cytarch.cellularity.shared.ui.theme

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.TextFieldColors
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontStyle
import androidx.compose.ui.text.font.FontWeight
import com.cytarch.cellularity.shared.settings.CellularityThemeComponents
import com.cytarch.cellularity.shared.settings.CellularityThemeColors
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.nunito_bold
import com.cytarch.cellularity.shared.generated.resources.nunito_italic
import com.cytarch.cellularity.shared.generated.resources.nunito_medium
import com.cytarch.cellularity.shared.generated.resources.nunito_regular
import org.jetbrains.compose.resources.Font

val PrimaryColor = Color(0xFF68CC4B)
val PrimaryContainer = Color(0xFF303030)
val SecondaryColor = Color(0xFF03DAC6)
val BackgroundColor = Color(0xFF505050)
val SurfaceVariant = Color(0xFF3A3A3A)
val OnSurfaceVariant = Color(0xFFD3D3D3)
val TextFieldTextColor = Color(0xFF45B436)

@Immutable
data class CellularityColorSet(
    val textFieldText: Color = TextFieldTextColor
)

object CellularityColors {
    val current: CellularityColorSet
        @Composable
        @ReadOnlyComposable
        get() = LocalCellularityColors.current
}

private val LocalCellularityColors = staticCompositionLocalOf { CellularityColorSet() }

@Composable
fun cellularityOutlinedTextFieldColors(): TextFieldColors {
    val textColor = CellularityColors.current.textFieldText
    return OutlinedTextFieldDefaults.colors(
        focusedTextColor = textColor,
        unfocusedTextColor = textColor,
        disabledTextColor = textColor.copy(alpha = 0.38f),
        errorTextColor = textColor
    )
}

data class CellularityDimensionSet(
    val none: Dp = 0.dp,
    val hairline: Dp = 1.dp,
    val xxSmall: Dp = 2.dp,
    val xSmall: Dp = 4.dp,
    val small: Dp = 6.dp,
    val medium: Dp = 8.dp,
    val large: Dp = 10.dp,
    val xLarge: Dp = 12.dp,
    val xxLarge: Dp = 14.dp,
    val xxxLarge: Dp = 16.dp,
    val sectionTop: Dp = 18.dp,
    val touchPadding: Dp = 24.dp,
    val cutoutTopPadding: Dp = 32.dp,
    val toolbarHeight: Dp = 56.dp,
    val compactControlHeight: Dp = 38.dp,
    val iconButtonSize: Dp = 40.dp,
    val swatchSize: Dp = 24.dp,
    val sliderThumbSize: Dp = 16.dp,
    val sliderTrackHeight: Dp = 4.dp,
    val cardCorner: Dp = 8.dp,
    val smallCorner: Dp = 4.dp,
    val tabCorner: Dp = 12.dp,
    val panelCorner: Dp = 16.dp,
    val cardElevation: Dp = 2.dp
)

object CellularityDimensions {
    val default = CellularityDimensionSet()
    val none = default.none
    val hairline = default.hairline
    val xxSmall = default.xxSmall
    val xSmall = default.xSmall
    val small = default.small
    val medium = default.medium
    val large = default.large
    val xLarge = default.xLarge
    val xxLarge = default.xxLarge
    val xxxLarge = default.xxxLarge
    val sectionTop = default.sectionTop
    val touchPadding = default.touchPadding
    val cutoutTopPadding = default.cutoutTopPadding
    val toolbarHeight = default.toolbarHeight
    val compactControlHeight = default.compactControlHeight
    val iconButtonSize = default.iconButtonSize
    val swatchSize = default.swatchSize
    val sliderThumbSize = default.sliderThumbSize
    val sliderTrackHeight = default.sliderTrackHeight
    val cardCorner = default.cardCorner
    val smallCorner = default.smallCorner
    val tabCorner = default.tabCorner
    val panelCorner = default.panelCorner
    val cardElevation = default.cardElevation
    val diagnosticsLineHeight = 15.sp
    val fontTiny = 10.sp
    val fontSmall = 12.sp
    val fontMedium = 14.sp
    val fontLarge = 16.sp
    val fontTitle = 20.sp

    val current: CellularityDimensionSet
        @Composable
        @ReadOnlyComposable
        get() = LocalCellularityDimensions.current
}

private val LocalCellularityDimensions = staticCompositionLocalOf { CellularityDimensions.default }

private fun CellularityThemeComponents.toDimensionSet(): CellularityDimensionSet {
    val normalized = normalized()
    return CellularityDimensions.default.copy(
        cardCorner = normalized.cardCorner.dp,
        cardElevation = normalized.cardElevation.dp,
        medium = normalized.itemSpacing.dp,
        xLarge = normalized.contentPadding.dp,
        large = normalized.listPadding.dp,
        iconButtonSize = normalized.iconButtonSize.dp,
        sliderThumbSize = normalized.sliderThumbSize.dp,
        sliderTrackHeight = normalized.sliderTrackHeight.dp,
        swatchSize = normalized.swatchSize.dp
    )
}

@Composable
fun ProvideCellularityDimensions(
    themeComponents: CellularityThemeComponents,
    content: @Composable () -> Unit
) {
    CompositionLocalProvider(LocalCellularityDimensions provides themeComponents.toDimensionSet()) {
        content()
    }
}

private val CustomLightColorScheme = lightColorScheme(
    primary = PrimaryColor,
    secondary = SecondaryColor,
    primaryContainer = PrimaryContainer,
    surfaceVariant = SurfaceVariant,
    onSurfaceVariant = OnSurfaceVariant,
    background = BackgroundColor
)

private val CustomDarkColorScheme = darkColorScheme(
    primary = PrimaryColor,
    secondary = SecondaryColor,
    primaryContainer = PrimaryContainer,
    surfaceVariant = SurfaceVariant,
    onSurfaceVariant = OnSurfaceVariant,
    background = BackgroundColor
)

private fun CellularityThemeColors.toLightColorScheme() = lightColorScheme(
    primary = parseThemeColor(primary, PrimaryColor),
    secondary = parseThemeColor(secondary, SecondaryColor),
    primaryContainer = parseThemeColor(primaryContainer, PrimaryContainer),
    surfaceVariant = parseThemeColor(surfaceVariant, SurfaceVariant),
    onSurfaceVariant = parseThemeColor(onSurfaceVariant, OnSurfaceVariant),
    background = parseThemeColor(background, BackgroundColor)
)

private fun CellularityThemeColors.toDarkColorScheme() = darkColorScheme(
    primary = parseThemeColor(primary, PrimaryColor),
    secondary = parseThemeColor(secondary, SecondaryColor),
    primaryContainer = parseThemeColor(primaryContainer, PrimaryContainer),
    surfaceVariant = parseThemeColor(surfaceVariant, SurfaceVariant),
    onSurfaceVariant = parseThemeColor(onSurfaceVariant, OnSurfaceVariant),
    background = parseThemeColor(background, BackgroundColor)
)

private fun parseThemeColor(value: String, fallback: Color): Color {
    val hex = value.trim().removePrefix("#")
    val rgb = hex.toLongOrNull(16) ?: return fallback
    return when (hex.length) {
        6 -> Color(0xFF000000 or rgb)
        8 -> Color(rgb)
        else -> fallback
    }
}

@Composable
private fun cellularityTypography(): Typography {
    val nunito = FontFamily(
        Font(Res.font.nunito_regular, weight = FontWeight.Normal),
        Font(Res.font.nunito_italic, weight = FontWeight.Normal, style = FontStyle.Italic),
        Font(Res.font.nunito_medium, weight = FontWeight.Medium),
        Font(Res.font.nunito_bold, weight = FontWeight.Bold)
    )
    val defaults = Typography()
    return Typography(
        displayLarge = defaults.displayLarge.copy(fontFamily = nunito),
        displayMedium = defaults.displayMedium.copy(fontFamily = nunito),
        displaySmall = defaults.displaySmall.copy(fontFamily = nunito),
        headlineLarge = defaults.headlineLarge.copy(fontFamily = nunito),
        headlineMedium = defaults.headlineMedium.copy(fontFamily = nunito),
        headlineSmall = defaults.headlineSmall.copy(fontFamily = nunito),
        titleLarge = defaults.titleLarge.copy(fontFamily = nunito),
        titleMedium = defaults.titleMedium.copy(fontFamily = nunito),
        titleSmall = defaults.titleSmall.copy(fontFamily = nunito),
        bodyLarge = defaults.bodyLarge.copy(fontFamily = nunito),
        bodyMedium = defaults.bodyMedium.copy(fontFamily = nunito),
        bodySmall = defaults.bodySmall.copy(fontFamily = nunito),
        labelLarge = defaults.labelLarge.copy(fontFamily = nunito),
        labelMedium = defaults.labelMedium.copy(fontFamily = nunito),
        labelSmall = defaults.labelSmall.copy(fontFamily = nunito)
    )
}

@Composable
fun CellularityTheme(
    darkTheme: Boolean = false,
    themeColors: CellularityThemeColors = CellularityThemeColors.Default,
    themeComponents: CellularityThemeComponents = CellularityThemeComponents.Default,
    content: @Composable () -> Unit
) {
    val colors = if (themeColors == CellularityThemeColors.Default) {
        if (darkTheme) CustomDarkColorScheme else CustomLightColorScheme
    } else {
        if (darkTheme) themeColors.toDarkColorScheme() else themeColors.toLightColorScheme()
    }
    val cellularityColors = CellularityColorSet(
        textFieldText = parseThemeColor(
            themeColors.textFieldText,
            TextFieldTextColor
        )
    )
    CompositionLocalProvider(LocalCellularityColors provides cellularityColors) {
        ProvideCellularityDimensions(themeComponents) {
            MaterialTheme(
                colorScheme = colors,
                typography = cellularityTypography(),
                content = content
            )
        }
    }
}
