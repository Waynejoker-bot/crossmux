# AirPage iGrowth Button Bridge

The iGrowth bridge turns an iGrowth-delivered AirPage BMP into a closed-loop
device interaction. It is active only while AirPage is open and only when the
displayed BMP carries the iGrowth delivery trailer and resolves to an exact
server-side action manifest.

Normal cold boots, deep-sleep wakes, and the first verified boot after an OTA
land directly in AirPage. If a valid current image is cached, AirPage reopens
that exact image and restores its matching action manifest; otherwise it opens
the pairing/upload QR screen. Firmware recovery, crash reporting, onboarding,
and explicit maintenance restarts retain their dedicated routes.

## Pairing

The AirPage device must already be bound to an iGrowth account. In **AirPage
Settings → iGrowth buttons**, the device asks `https://igrowth.cc` for an
eight-character pairing code. The user confirms that code in the same
account's AirPage settings. The device polls until it can claim a
device-scoped HMAC secret.

Pairing and every signed request use verified TLS. The device secret is tied to
the hardware identity before it is written to SD card, which deters casual
copying but is not hardware-backed encryption. Unbinding or rebinding in
iGrowth revokes the server-side credential and remains authoritative.

## Delivery and action contract

iGrowth appends a 55-byte non-pixel trailer to each actionable BMP:

1. `IGROWTH-AIRPAGE\0v1\0`
2. the one-based page number as a little-endian `uint32_t`
3. a 32-byte server tag

The firmware hashes the complete BMP, including this trailer, and requests the
manifest for that exact hash. The v1 manifest must contain exactly these four
logical-button slots, in order. The button and action ID are the stable protocol
contract; the signed label is frozen by iGrowth for that delivery and is shown
verbatim in the device footer:

| Logical button | Action ID | Label source |
|---|---|---|
| Back | `dismiss` | Model-generated choice A |
| Confirm | `continue` | Model-generated choice B |
| Left | `explain` | Model-generated choice C |
| Right | `next` | Model-generated choice D |

The labels are required, non-empty UTF-8 strings shorter than 49 bytes and may
not contain control characters. They are cached with the exact delivery,
binding revision, image hash, and page so offline rendering cannot mix choices
from another turn. The mappings use `MappedInputManager` logical buttons, so
the user's front-key remapping remains authoritative. When an actionable image
is displayed, a short Back press sends slot A (`dismiss`); holding Back for one
second exits to the normal AirPage QR screen and consumes the release so it
cannot trigger a second action.

On the server, the fixed action ID resolves the label frozen for that delivery.
The label becomes the next user message in the original iGrowth Session; the
Agent continues that same Session and its reply is delivered back to the same
AirPage device. The firmware never invents or substitutes the four labels.

Each event carries the delivery ID, complete-image SHA-256, page number,
logical button, action ID, a monotonic sequence, a random event ID, and the
device timestamp. The JSON body is authenticated with HMAC-SHA256 over the
versioned canonical request. iGrowth binds the event back to the exact active
delivery, account, and source session before dispatching it.

## Offline behavior and storage

The most recent verified manifest is cached only for the same binding revision,
image hash, and page. When Wi-Fi is unavailable, a matching page can still
enqueue an action body, which is signed when it is posted. The outbox is capped
at eight JSON files; a ninth action fails visibly instead of discarding an older
intent. It drains oldest-first while AirPage is foregrounded and Wi-Fi is connected. A server 4xx
response settles the item because retrying cannot repair a revoked credential,
stale delivery, or invalid event.

Runtime files live under `/.crosspoint/airpage/igrowth/`:

- `credential`: binding revision plus the device-tied obfuscated secret
- `sequence`: last allocated event sequence
- `manifest`: the last verified delivery/hash/page tuple
- `outbox/<20-digit-sequence>.json`: at most eight pending event bodies

Network response bodies are capped at 2 KiB and events at 768 bytes. Image
hashing uses one bounded 512-byte heap buffer; outbox replay uses one bounded
769-byte heap buffer. Both allocations are transient and checked before use.

Deep sleep still requires the normal hardware wake gesture before AirPage can
receive a button press or drain its outbox. Server acceptance confirms receipt,
not that a later Agent reply has already refreshed the physical screen.
