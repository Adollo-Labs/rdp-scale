# Adollo cross-project agent discovery

This repository keeps its own runtime and engineering contracts. In addition, all Adollo chat agents must know the organization-wide [ChatGPT runtime -> Google Drive file transport](https://github.com/Adollo-Labs/.github/blob/main/docs/CHATGPT_GOOGLE_DRIVE_BINARY_TRANSPORT.md).

When ChatGPT creates a **new local file** that must be published to connected Google Drive, prefer the verified direct Files route:

    exact runtime container_path -> Files upload -> /Google Drive/...

For binary artifacts, close/flush first, record size and SHA-256, upload the binary before any READY/result marker, and let the receiver independently verify the bytes. Do not invent bare file_uri/file_/sediment:// identifiers and do not serialize binary data to text as a workaround.

This is a cross-project discovery rule only; it does not change this application's runtime architecture or local transport unless a task actually needs ChatGPT -> Google Drive file publication.

## Interrupted ChatGPT work: recover before replay

After a ChatGPT stream error, UI Retry, lost tool response, restored context, or a continuation of an earlier attempt, follow the [Adollo Labs interrupted-work recovery contract](https://github.com/Adollo-Labs/.github/blob/main/docs/CHATGPT_INTERRUPTED_WORK_RECOVERY.md) **before repeating any external action**. Recover the original durable job/request/run identity and inspect its owning system first. A missing chat response is not evidence that dispatch, upload, install, or another side effect failed. Completed work is reported, pending work is reconciled under the same identity, and ambiguous identity blocks duplicate writes. A genuinely new explicit user request remains new work according to the product contract.
