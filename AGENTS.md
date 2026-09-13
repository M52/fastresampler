# File format ownership

Read [FORMATS.md](FORMATS.md) before changing FSI, FSB, FSP or SMFP.
Their schemas, binary layouts, persisted IDs and reusable serialization live
in the separate `3rdparty/fastsampler-shared` Git submodule. Make format
changes there, commit and publish them first, and update the pinned commit
in every affected consumer with compatibility checks.
Preserve unrelated local work.
