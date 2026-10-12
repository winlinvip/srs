---
name: internal-obs-control
description: Drive OBS Studio from the command line over obs-websocket - launch and quit it, manage profiles, scene collections, scenes and sources, set the stream service and start or stop streaming - to go live or to publish test streams to SRS or any other RTMP, SRT or WebRTC WHIP platform, including several isolated OBS instances in parallel. Use when a task involves OBS, obs-websocket, "stream from OBS", an OBS profile or scene, or an automated OBS publisher.
---

# OBS Control

Control OBS Studio programmatically: as a repeatable publisher for testing SRS, for testing other live
streaming platforms, or for a real live stream. Automated tests use an isolated OBS instance
([Parallel tests](#parallel-tests-one-isolated-instance-per-test)); the user's main OBS is for interactive
sessions and live streams the user watches.

Two control surfaces exist and this skill uses both, in this order of preference:

1. **obs-websocket 5** (bundled with OBS 28+; port 4455 on the main OBS, 4460 and up on an instance, or
   the `OBS_CONTROL_WS_PORTS` range): everything while OBS runs - profiles, scene
   collections, scenes, inputs, stream service, start/stop, status, screenshots.
2. **Config files** (`basic.ini`, `service.json`, scene collection JSON, `user.ini`): pre-seeding a machine
   before OBS starts and the few things the websocket cannot do (remove a scene collection, change output
   mode without a profile switch). Edit them only while OBS is not running; OBS overwrites the current
   profile and collection from memory.

The **obs-websocket plugin is required**. It ships inside OBS (`obs-websocket.plugin` / `.dll` / `.so`)
so nothing is downloaded; it only has to be *enabled* in its `config.json`, which OBS reads at startup.
Step 1 checks this and fixes it.

## Running the scripts

- `scripts/` and `references/` resolve relative to the directory containing this `SKILL.md`.
- Run scripts with `python3` (3.9+, standard library only; no pip packages). They work on macOS, Linux and
  Windows.
- Every script bounds its own waits, so do not wrap them in coreutils `timeout` (absent on macOS).
- The user's SRS repository is the current working directory when this skill runs inside it; SRS itself may
  be the local build `trunk/objs/srs` or a remote server. Never `cd` away permanently.

The scripts find OBS and its config themselves. These variables override that, for the main OBS:

- **`OBS_BINARY`**: the OBS executable to launch.
- **`OBS_CONFIG_DIR`**: the OBS config directory, the one holding `basic/` and `plugin_config/`.
- **`OBS_WS_HOST`**: the websocket host, default `127.0.0.1`.
- **`OBS_WS_PORT`**: the websocket port, default from the websocket `config.json`, else 4455.
- **`OBS_WS_PASSWORD`**: the websocket password, default from the websocket `config.json`.

These variables select a run or an instance:

- **`OBS_RUN`**: the run id, same as `--run`; see [Runs](#runs-every-resource-carries-a-run-id).
- **`OBS_INSTANCE`**: the instance id, same as `--instance`; `OBS_WS_HOST`, `OBS_WS_PORT` and
  `OBS_WS_PASSWORD` are then ignored. See [Parallel tests](#parallel-tests-one-isolated-instance-per-test).
- **`OBS_CONTROL_HOME`**: the state root, default `$HOME/.cache/internal-obs-control`, holding `instances/`
  and `runs/`.
- **`OBS_CONTROL_WS_PORTS`**: the inclusive websocket port range for new instances, such as `15000-15099`;
  default `4460-5459`. Use `<first>-<last>` with ports from 1 to 65535.

## Runs: every resource carries a run id

Every profile, scene collection, scene and source this skill creates belongs to a **run** and carries its
id in the name (`SRS Test r3f9a2`, `SRS Desktop r3f9a2`, `Camera r3f9a2`). The run's ledger
(`$HOME/.cache/internal-obs-control/runs/<id>/run.json`) lists the profiles and collections the run created and the
profile/collection that were current before it. Cleanup removes **only what the ledger lists**, then
switches back to the previous ones. A name match is never proof of ownership: the user may have an "SRS
Test" or "SRS Desktop" of their own, and another agent's run may be live in the same OBS.

```bash
RUN=$(python3 scripts/obsws.py run new)                  # claims a unique id, e.g. r3f9a2
python3 scripts/obs_srs_test.py setup --run $RUN --profile "SRS Desktop" --rtmp 'rtmp://localhost/live/{run}'
python3 scripts/obsws.py run list                        # every run and what it owns
python3 scripts/obs_srs_test.py teardown --run $RUN      # remove exactly what $RUN created
```

- `setup` without `--run` claims a new run and prints `run: rXXXXX`; pass that id (or `OBS_RUN`) to every
  later `setup` and to `teardown`. Get ids from `run new`, never invent one. With `--instance`, the
  instance id is the run id.
- `--profile`, `--collection` and `--scene` are base names; the run id is always appended.
- `setup` refuses an existing profile or collection with the run's name that the ledger does not list,
  and `teardown` without `--run` refuses and lists the runs.
- **Resources made by hand** (`obsws.py call CreateInput ...`, as for a desktop/camera/mic scene) follow
  the same rule: name them `<base> <run>`, put inputs and scenes only in the run's own collection (they
  go away with it), and record any profile or collection created by hand with
  `obsws.py run add --run $RUN --kind profile|collection --name "<name> <run>"`.
- Never delete a profile, collection or its files by name (`RemoveProfile`, `rm` in `basic/`) outside
  `teardown`. If the user asks to clean up something the ledger does not list, show them what matched and
  ask first.

## Parallel tests: one isolated instance per test

One OBS has one stream output, one current profile and one scene collection, so two tests can never share
an OBS. For anything that may run next to another test (several agents, a matrix of protocols, 10 OBS at
once), give each test its own **instance** with `--instance ID` (or `OBS_INSTANCE=ID`) on every command:

```bash
ID=$(python3 scripts/obsws.py instance new)             # claims a unique id, e.g. t3f9a2
python3 scripts/obsws.py launch --instance $ID --wait 90 --minimize-to-tray
python3 scripts/obs_srs_test.py setup --instance $ID --rtmp 'rtmp://localhost/live/{instance}' --start --verify
python3 scripts/obs_srs_test.py stream status --instance $ID
python3 scripts/obsws.py instance list                   # id, pid, websocket port, config dir
python3 scripts/obsws.py instance remove --instance $ID  # quit it and delete its config
```

How to handle the instance id:

- **Always get the id from `instance new`; never invent one.** Hand-picked ids such as `t1` collide when
  two agents or test runs pick the same one, and then both drive one OBS.
- **Ids are unique on this machine.** `instance new` makes `t` plus 5 random hex digits and claims it by
  creating its directory atomically. An id already taken is skipped, and ids made at the same moment
  still differ.
- **Ids across machines** can repeat in rare cases. When several machines publish to one SRS, the
  stream-name check below catches it.
- **One test per running instance.** `launch` refuses an instance that is already running, unless
  `--reuse` says this test launched it.
- **Keep the id for the whole test.** If the agent's shell does not keep variables, write the printed id
  literally into each later command.
- **Remove the instance at the end** with `instance remove --instance <id>`.
- **Never use `instance remove --all` during tests.** It also removes the instances of *other* running
  tests; use it only when nothing else is testing.

What an instance isolates, and why each part matters:

- **Config dir** `$HOME/.cache/internal-obs-control/instances/<id>/` (`OBS_CONTROL_HOME` moves the root). OBS derives
  it from `CFFIXED_USER_HOME` on macOS and `XDG_CONFIG_HOME` on Linux, so profiles, scene collections,
  `user.ini` (current profile/collection), crash sentinels, logs and browser-source cache are private.
  The user's own OBS config is never read or written. Windows OBS has no such override: not supported.
- **Websocket port and password**, allocated under a lock from `OBS_CONTROL_WS_PORTS` (default `4460-5459`), skipping ports that other
  instances or the main OBS use, and stored in the instance's `config.json`. `OBS_WS_HOST`, `OBS_WS_PORT` and `OBS_WS_PASSWORD` are
  ignored in instance mode because they describe the main OBS.
  Allocation fails with the exhausted range when no port is free. Existing instances keep their port
  across relaunches; set the range before launching a new instance.
- **Process**: a pid file per instance, so `quit` sends SIGTERM to exactly that OBS (the AppleScript
  quit addresses the bundle id, which every instance shares). `--multi` is added automatically so no
  "already running" dialog appears, and the main OBS's `probe`/`quit`/`launch` skip instance processes.
- **Startup dialogs**: a new instance is seeded with `FirstRun=true`, `ConfirmOnExit=false`, the main
  install's `LastVersion` and `MacOSPermissionsDialogLastShown`, so no wizard, permission or exit dialog blocks it.
- **Names**: the instance id is the run id, so profile, collection, scene and sources get it
  (`SRS Test t3f9a2`, `SRS Media t3f9a2`) and logs, screenshots and SRS clients show which test they belong to.
- **SRS stream name**: `{instance}` (or `{run}`) in `--rtmp`, `--srt`, `--whip` or `--stream` expands to the id. Two publishers
  on one SRS stream collide there even with isolated OBS, so `setup --start` refuses a stream that SRS
  already shows as published by another client.
- **Shared temp files** (the ffmpeg test clip, the clock page) are written to a temp file and renamed, so
  a parallel setup never loads a half-written file.

Running many instances at once:

- **CPU is the limit.** Ten x264 encoders at 720p30 saturate a laptop CPU.
- **Lower the load** with smaller `--width`, `--height` and `--video-bitrate`.
- **Or use the hardware encoder** with `--encoder apple_h264`. Hardware encoder sessions are limited, so
  it does not scale without bound either.
- **Check for skipped frames** with `obsws.py call GetStats --instance <id>`. Skipped frames mean the
  machine is overloaded and bitrate numbers are not trustworthy.

Without `--instance`:

- **The scripts drive the user's main OBS**, still under a run id.
- **Use it only** for a single interactive test, or a live session the user watches. Never for automated
  or parallel tests.

## Step 1: Preflight (always)

```bash
python3 scripts/obsws.py probe            # add --instance ID for an isolated instance
```

It checks OBS from the outside first, then over the websocket:

- **Install**: the OBS binary and version, and the obs-websocket plugin file.
- **Config on disk**: the config directory, the profiles and scene collections, and the current ones from
  `user.ini`.
- **Websocket config**: enabled or not, port, whether a password is required.
- **Process**: whether OBS is running, its pids, stale crash sentinels, and the newest log.
- **Live handshake**: the OBS and obs-websocket versions, and OBS's own profile and scene collection lists.
  While OBS runs, trust these lists; the files on disk can lag behind.

The last line starts with `NEXT:` or `READY:` and says what to do. Act on it:

| probe says | do |
| --- | --- |
| OBS not installed | ask the user to install OBS Studio 28+ (obs-websocket is included); do not try to build it |
| websocket disabled | `python3 scripts/obsws.py enable-websocket` then restart OBS: `quit` + `launch --wait 90` (or `enable-websocket --restart`) |
| OBS not running | `python3 scripts/obsws.py launch --wait 90` (add `--profile`, `--collection`, `--scene`, `--minimize-to-tray`); an instance's `launch` also creates its config |
| running but not answering | a modal dialog is probably open (crash recovery, permissions, exit confirmation); look at the newest log, ask the user to dismiss it, or `quit --force` + `launch` |
| READY | continue |

## Step 2: Build the test setup

For the common case (publish a synthetic scene to SRS) use the recipe, which is idempotent and safe to
re-run with different options:

```bash
# RTMP
python3 scripts/obs_srs_test.py setup --rtmp rtmp://localhost/live/livestream --start --verify
# WHIP (WebRTC); adds eip=127.0.0.1 for localhost so SRS advertises a candidate OBS can reach
python3 scripts/obs_srs_test.py setup --whip 'http://localhost:1985/rtc/v1/whip/?app=live&stream=livestream' --start --verify
# SRT
python3 scripts/obs_srs_test.py setup --srt srt://127.0.0.1:10080 --srt-streamid '#!::r=live/livestream,m=publish' --srs-api http://localhost:1985 --start --verify
```

Destination fields:

- SRT `--srt-streamid` is the Stream Key; `--srt-passphrase` is the Password and enables `use_auth`.
  OBS maps these in `plugins/rtmp-services/rtmp-custom.c:149–163`.
- An SRT URL `streamid=` (literal `#!::` or URL-encoded) takes precedence and is moved into the key;
  other URL options stay in the server (`plugins/obs-ffmpeg/obs-ffmpeg-srt.h:769–775`).
- RTMP `--rtmp rtmp://host/app/stream?x=y` and `--rtmp-server rtmp://host/app --rtmp-key 'stream?x=y'`
  retain the query in the key. Verification uses `app/stream` without the key's query parameters.

What it does, in order (each step is also available as a single `obsws.py call`):

1. `GetVersion`, refuse to reconfigure while streaming unless `--stop-first`.
2. Profile: `CreateProfile` (clean defaults, switches to it) or `SetCurrentProfile`; `SetVideoSettings`
   (canvas/output/fps); `SetProfileParameter` for `Output/Mode=Simple`, `SimpleOutput/VBitrate`,
   `ABitrate`, `StreamEncoder`, `StreamAudioEncoder` (opus for WHIP, aac otherwise), `Preset`.
3. `SetStreamServiceSettings` (`rtmp_custom` for RTMP/SRT, `whip_custom` for WHIP), **then** re-activate the
   profile by switching to another one and back. Activation is when OBS rebuilds the output handler
   (mode, encoder ids, service binding) and re-derives simple-mode encoder defaults from the selected
   service's codecs, so service first, reload second; otherwise a previously selected WHIP service drags
   the audio encoder back to Opus.
4. Scene collection: `CreateSceneCollection` or `SetCurrentSceneCollection` (both block until loaded).
5. Scene: `CreateScene` + `SetCurrentProgramScene`.
6. Sources: `CreateInput`/`SetInputSettings` + `SetSceneItemTransform`: a canvas-sized colour background,
   a looping ffmpeg-generated test clip with a 440 Hz tone (real motion and audio, so bitrate numbers mean
   something), and a text label; optionally a browser clock or an image.
7. `--start`: `StartStream` and wait for `outputActive`; `--verify`: poll the SRS HTTP API
   (`/api/v1/streams/`) until `publish.active` and print codec, resolution, kbps and play URLs. SRS's
   `kbps.recv_30s` is a trailing 30 s average and reads 0 right after start: add `--bitrate` (waits for it)
   or re-run `verify` later when the report needs a bitrate figure.

Then observe and steer:

```bash
python3 scripts/obs_srs_test.py stream status|start|stop|restart
python3 scripts/obs_srs_test.py verify --srs-api http://localhost:1985 --app live --stream livestream
python3 scripts/obsws.py screenshot --out /tmp/program.png --width 640 --height 360   # see what is streamed
python3 scripts/obsws.py events --types StreamStateChanged --timeout 60                 # reconnect behaviour
python3 scripts/obsws.py call GetStats
python3 scripts/obs_srs_test.py teardown --run $RUN   # remove what the run created, restore the previous profile
```

For anything the recipe does not cover, call requests directly. Names are exact and case-sensitive; input
names are global (not per scene); most requests accept `...Name` or `...Uuid`:

```bash
python3 scripts/obsws.py call GetInputKindList
python3 scripts/obsws.py call CreateInput '{"sceneName":"SRS Scene r3f9a2","inputName":"Cam r3f9a2","inputKind":"macos-avcapture","inputSettings":{}}'
python3 scripts/obsws.py call SetInputSettings '{"inputName":"SRS Label","inputSettings":{"text":"take 2"}}'
python3 scripts/obsws.py call SetStreamServiceSettings '{"streamServiceType":"rtmp_custom","streamServiceSettings":{"server":"rtmp://srs:1935/live","key":"k?secret=x","use_auth":false}}'
python3 scripts/obsws.py batch '[{"requestType":"StartStream"},{"requestType":"Sleep","requestData":{"sleepMillis":5000}},{"requestType":"GetStreamStatus"},{"requestType":"StopStream"}]'
```

How to read the result:

- **Success**: `call` prints the response data as JSON and exits 0.
- **Refused by OBS**: `call` prints the request type and `requestStatus` (`code`, `comment`) and exits 1.
  `batch` prints every result and exits 1 if any request failed.
- **Find the cause**: look the code up in the status code table in `references/websocket-protocol.md`,
  such as 600 for a wrong name or 601 for an input name already taken.
- **Fix the cause, don't retry blindly.** The one code worth a plain retry is 207 (`NotReady`, OBS still
  loading).
- **Other exit codes** come from the script, not from OBS: 2 cannot connect, 3 wrong websocket password,
  4 bad local state, 5 timed out. The error is printed on stderr.

## Step 3: Report

Report OBS and SRS separately.

What OBS did:

- **Versions**: OBS and obs-websocket.
- **Setup**: the instance or run id, the profile, the scene collection and the scene.
- **Target**: the URL, and the stream key or SRT streamid.
- **Encoding**: the video and audio encoders and bitrates.
- **Stream status** from `GetStreamStatus`: bytes, frames, congestion, reconnecting.
- **On failure**: the path of the newest OBS log.

What SRS saw, from its API:

- **Publish**: `publish.active`.
- **Media**: video and audio codecs, resolution, kbps.

What you changed or worked around:

- **Dialogs** you had to dismiss or avoid.
- **The user's own OBS**: enabled websocket, restarted OBS, created or removed profiles or scene
  collections.

## Gotchas

- **Stream restart**: use `obs_srs_test.py stream restart` to stop, wait for inactive plus
  one second, then start and wait for active. `stream start` also waits before starting an
  inactive output. If start times out with no new OBS log output, it switches away from the
  current profile and back, then retries once; at least two profiles must exist.

## Reference material (load as needed)

- `references/websocket-protocol.md` - handshake/auth, message envelopes, status codes, every request the
  automation relies on with exact field names, events, and behaviour learned from the source (queued
  profile creation, no RemoveSceneCollection, service merge semantics). Read when writing raw `call`s or
  when a request fails.
- `references/config-files.md` - where the config dir is per OS, `user.ini`/`global.ini`, `basic.ini` keys
  for output/video/audio, `service.json` variants (RTMP/SRT/WHIP/known services), `streamEncoder.json`,
  scene collection JSON structure, obs-websocket `config.json`, crash sentinel, logs, and an offline
  pre-seeding template. Read when OBS is not running or a setting does not stick.
- `references/sources-and-encoders.md` - input kinds per platform, settings for colour/text/media/browser/
  image/capture sources, audio routing, scene item transforms, encoder ids and keys, and how to pick a
  composition for bitrate, latency, reconnect and codec tests. Read before adding sources beyond the recipe.
- `references/srs-targets.md` - RTMP/SRT/WHIP URL formats for SRS, required SRS config, verification via
  the SRS API and playback URLs, the WHIP candidate override (`eip`), and a symptom -> cause table. Read when the
  destination is SRS and the stream does not show up.
- `references/cli-and-lifecycle.md` - binary locations, all command line flags, startup dialogs that block
  automation, graceful stop per OS, headless/CI notes, version differences (OBS 27 vs 28+, 31+, 32.x).
  Read before launching OBS on a new machine or debugging a launch that hangs.
