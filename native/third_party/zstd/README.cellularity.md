# Zstandard source snapshot

This directory contains the compression and decompression library sources from
Zstandard 1.5.7, upstream commit
`f8745da6ff1ad1e7bab384bd1f9d742439278e99`.

Upstream: <https://github.com/facebook/zstd>

Only the public headers and the `common`, `compress`, and `decompress` source
directories are retained. Dictionary training, command-line tools, tests,
deprecated APIs, and legacy frame decoders are not part of the application
build. The bundled source is used under the BSD license in `LICENSE`.
