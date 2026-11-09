/*
 * Cellularity source code.
 */

package com.cytarch.cellularity.desktop.files

import java.nio.file.Path

/**
 * A file the user pointed the application at from outside its storage directory.
 *
 * This is the desktop's counterpart of the phone's resolved content URI. On Android a share sheet
 * or a file manager hands over an opaque URI, which the adapter has to ask the content resolver
 * about before anything can be stored; on Windows the same gesture -- "Open with", a file dropped
 * on the launcher, a path on the command line -- arrives as a path. What has to be decided before
 * the file can be brought in is the same in both cases: the name it will be shown under and which
 * collection it belongs to.
 *
 * [fileName] is the name it will have inside the storage directory, extension included. It is
 * decided once, by the rules the rest of the storage uses, so a name that would leave the
 * directory is refused there rather than here and the two cannot disagree.
 */
data class DesktopExternalFile(
    val source: Path,
    val displayName: String,
    val fileName: String,
    val kind: DesktopExternalFileKind
)

/** Which collection an outside file is brought into. */
enum class DesktopExternalFileKind {
    SUBSTRATE,
    GENOME
}
