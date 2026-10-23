# Release-format test data

`genome-v1-golden.hex` is the exact uncompressed CLTY standalone-genome
fixture shared by the C++ and Kotlin codec tests.

`substrate-v1-golden.hex` is the exact uncompressed CLTY substrate fixture
owned by the native release substrate codec test. It pins canonical section,
column, chunk, directory, metadata, and deterministic pooled-genome encoding.

`registry-v1.tsv` records the stable release-v1 field registry used by the
codec contract tests.
