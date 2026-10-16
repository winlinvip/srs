# Smells

Mistakes an agent has made in this project, or is likely to, one line each. Where a commit showed it, the commit is named. The auditor reads this in phase 2, after its blind read, and reports each match with the file and line.

Add a line whenever the user's read catches something the audit missed, with the commit. Do not add lines from guesswork.

## State and lifetime

- **Transient state.** The same thing is added and removed in one function, around an event that reads it, so the event sees a state no client ever sees. `452987524`: an RTSP client added to the stats for its `on_play` at DESCRIBE and removed right after, though it stays connected.
- **Lifetime split across protocols.** A client, session or stream exists at different steps in different protocols for no protocol reason. Compare with the sibling: RTMP, HTTP-FLV, HLS, SRT, WebRTC, RTSP, GB28181.
- **Stream recreated.** A stats or source object is freed and made again inside one client's life, so ids change between two hooks of the same client.
- **Use after the owner is gone.** A pointer to a session, connection or source kept past the point where its owner can free it; a hook or a coroutine that runs after the object it names is released.

## Tests

- **Test asserts the implementation.** The expected values describe what the diff does, not what the protocol says, such as a state right after an event that the protocol says does not change it. `452987524`'s `RtspOnPlayCountsPlayer`: the hook sees 2 clients, then the stats hold 1.
- **One moment, not a lifetime.** A single number at a single instant, where the expectation is the state after each step: after the start, after a refusal, after the play, after the close.
- **Widened limit.** A longer wait, a retry, a sleep or a bigger tolerance as a flake fix, with no cause named in SRS's own timing.
- **Diagnosis "the test, not SRS".** A flake fixed on the test side with the claim that SRS is within its spec: check that the spec is quoted and that SRS's timing was measured, not assumed.
- **Mock covers the behavior under test.** The mock answers what the code is supposed to compute, so the test passes whatever the code does. `f4952d274` fixed utests that depended on freed memory and 1 ms sleeps.
- **Tests named, not asserting.** The message lists tests that do not check what the message says they check.

## Message and comments

- **"Everything else unchanged."** Claimed for a bug whose honest fix should move something visible: an API listing, another protocol's hook, a default. `452987524`: "every other hook and client are unchanged", while the client's presence in the API should have changed.
- **Comment that justifies.** A comment that gives a reason for the shape of the code instead of saying what it does; the reason usually covers a gap. `452987524`: "It leaves the stats after the hook, for only PLAY makes the player, which adds it again."
- **Decision presented as given.** A design choice settled in passing, such as when a client exists or which layer owns a check, with no option named. `452987524`: a client is not listed until PLAY.

## Scope and copies

- **One copy of two.** The fix lands in one of two copies of the same logic: the UDP caster and GB28181, HLS TS and fmp4, HLS and DASH, WHIP and the legacy `/rtc/v1/` API, RTMP and RTMPS. `e6295c3cb` fixed the caster's H.264 NALU framing and left the GB28181 copy for a later row.
- **Sibling mounts.** A fix for HTTP-FLV that does not reach HTTP-TS, MP3 and AAC, or a fix for one player type that the security or the hooks do not see for the others.
- **Wrong layer.** An error code, a name or a check from another protocol's layer, such as a WebRTC error for an RTMP refusal; a check in the connection that belongs to the source, or the reverse.
- **Unrelated change folded in.** A rename, a cleanup or a second fix in the same commit, so the review cannot judge them apart.

## Code shape

- **Guards that do not match the surrounding code.** A re-lookup, a null check or a two-line helper where the nearby code uses the object directly; it hides a lifetime question instead of answering it (R37).
- **Per-packet work on the hot path.** A scan, a lookup or an allocation per RTP or RTCP packet where the code around it caches.
- **Iterator element not named.** `it->second->` or `(*it)->` where the surrounding code names the element first.
