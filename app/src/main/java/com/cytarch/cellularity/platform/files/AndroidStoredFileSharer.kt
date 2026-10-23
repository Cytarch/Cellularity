package com.cytarch.cellularity.platform.files

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import com.cytarch.cellularity.R
import com.cytarch.cellularity.shared.files.CellularityFileNames
import com.cytarch.cellularity.shared.files.StorageError
import com.cytarch.cellularity.shared.files.StorageErrorCode
import com.cytarch.cellularity.shared.files.StorageResult
import com.cytarch.cellularity.shared.files.StoredFileShareOutcome
import com.cytarch.cellularity.shared.files.StoredGenomeId
import com.cytarch.cellularity.shared.files.StoredSubstrateId
import com.cytarch.cellularity.shared.generated.resources.Res
import com.cytarch.cellularity.shared.generated.resources.sharing_email_chooser_title
import com.cytarch.cellularity.shared.generated.resources.sharing_message_with_app_name_format
import com.cytarch.cellularity.shared.generated.resources.sharing_substrate_email_body
import com.cytarch.cellularity.shared.generated.resources.sharing_substrate_email_subject
import org.jetbrains.compose.resources.getString

/** Owns Android URI publication and share-sheet transport for stored Cellularity files. */
internal class AndroidStoredFileSharer(
    private val context: Context,
    private val fileRepository: AndroidCellularityFileRepository
) {
    suspend fun shareSubstrate(id: StoredSubstrateId): StoredFileShareOutcome {
        val subject = getString(Res.string.sharing_substrate_email_subject)
        val message = getString(
            Res.string.sharing_message_with_app_name_format,
            getString(Res.string.sharing_substrate_email_body),
            context.getString(R.string.app_name)
        )
        return share(
            filename = CellularityFileNames.substrateFileName(id.value)
        ) { uri ->
            Intent(Intent.ACTION_SEND).apply {
                type = "message/rfc822"
                putExtra(Intent.EXTRA_SUBJECT, subject)
                putExtra(
                    Intent.EXTRA_TEXT,
                    message
                )
                putExtra(Intent.EXTRA_STREAM, uri)
                addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            }
        }
    }

    suspend fun shareGenome(id: StoredGenomeId): StoredFileShareOutcome = share(
        filename = id.fileName
    ) { uri ->
        Intent(Intent.ACTION_SEND).apply {
            type = "application/octet-stream"
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
    }

    private suspend fun share(
        filename: String,
        createIntent: (Uri) -> Intent
    ): StoredFileShareOutcome {
        val uri = when (val result = fileRepository.shareUriForInternalFile(filename)) {
            is StorageResult.Success -> result.value
            is StorageResult.Failure -> return StoredFileShareOutcome.Failure(result.error)
        }

        return try {
            context.startActivity(
                Intent.createChooser(
                    createIntent(uri),
                    getString(Res.string.sharing_email_chooser_title)
                )
            )
            StoredFileShareOutcome.Started
        } catch (_: ActivityNotFoundException) {
            StoredFileShareOutcome.NoCompatibleTarget
        } catch (failure: SecurityException) {
            StoredFileShareOutcome.Failure(
                StorageError(
                    StorageErrorCode.PERMISSION_REVOKED,
                    failure.message.orEmpty()
                )
            )
        } catch (failure: RuntimeException) {
            StoredFileShareOutcome.Failure(
                StorageError(StorageErrorCode.IO_FAILURE, failure.message.orEmpty())
            )
        }
    }
}
