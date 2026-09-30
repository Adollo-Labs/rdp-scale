# Adollo cross-project agent discovery

This repository keeps its own runtime and engineering contracts. In addition, all Adollo chat agents must know the organization-wide [ChatGPT runtime -> Google Drive file transport](https://github.com/Adollo-Labs/.github/blob/main/docs/CHATGPT_GOOGLE_DRIVE_BINARY_TRANSPORT.md).

When ChatGPT creates a **new local file** that must be published to connected Google Drive, prefer the verified direct Files route:

    exact runtime container_path -> Files upload -> /Google Drive/...

For binary artifacts, close/flush first, record size and SHA-256, upload the binary before any READY/result marker, and let the receiver independently verify the bytes. Do not invent bare file_uri/file_/sediment:// identifiers and do not serialize binary data to text as a workaround.

This is a cross-project discovery rule only; it does not change this application's runtime architecture or local transport unless a task actually needs ChatGPT -> Google Drive file publication.
