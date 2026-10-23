package com.cytarch.cellularity.ui.main_menu.screens.about

import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.about_version_unknown
import com.cytarch.cellularity.shared.ui.main_menu.about.AboutScreen as SharedAboutScreen
import org.jetbrains.compose.resources.stringResource

@Composable
fun AboutScreen() {
    val context = LocalContext.current
    val unknownVersion = stringResource(Res.string.about_version_unknown)
    val versionName = try {
        val packageInfo = context.packageManager.getPackageInfo(context.packageName, 0)
        packageInfo.versionName ?: unknownVersion
    } catch (_: Exception) {
        unknownVersion
    }

    SharedAboutScreen(
        versionName = versionName
    )
}
