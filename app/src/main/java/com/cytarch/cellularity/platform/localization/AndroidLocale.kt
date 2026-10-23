package com.cytarch.cellularity.platform.localization

import android.content.Context
import androidx.core.os.ConfigurationCompat
import java.util.Locale

/** Returns the locale attached to this Android resource context on every supported API level. */
internal fun Context.activeResourceLocale(): Locale =
    ConfigurationCompat.getLocales(resources.configuration)[0] ?: Locale.getDefault()
