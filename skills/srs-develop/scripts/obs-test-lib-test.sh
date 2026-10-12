#!/bin/bash
# Regression checks for the sourced harness; --live also exercises real RTMP.
set -eu
SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
LIB="$SCRIPT_DIR/obs-test-lib.sh"
if [[ ! -f "$LIB" ]]; then
  echo 'FAIL shared OBS harness is missing'
  exit 1
fi
source "$LIB"
if [[ "${1:-}" == --live ]]; then
  obs_test_init helper 15100
  obs_test_start
  obs_test_setup --rtmp "rtmp://127.0.0.1:$OBS_RTMP_PORT/live/$OBS_INSTANCE_ID"
  obs_test_assert_api H264 AAC
  obs_test_play http-flv aac
  obs_test_play hls aac
  obs_test_whep
  obs_test_finish
  exit
fi
checks=0
check() {
  checks=$((checks + 1))
  if "$@"; then echo "PASS regression $checks"; else echo "FAIL regression $checks"; exit 1; fi
}
disabled() {
  local output
  output=$(SRS_TEST_OBS=0 obs_test_init disabled 15100)
  [[ "$output" == 'SKIP SRS_TEST_OBS=0' ]]
}
check disabled
check obs_test_probe_available '{"obs_binary":"OBS","websocket_plugin":"plugin"}'
check test "$(obs_test_probe_available '{"obs_binary":null,"websocket_plugin":null}'; echo $?)" = 1
obs_test_read_stream() { printf '%s\n' "$fixture"; }
OBS_INSTANCE_ID=test
OBS_API_URL=http://127.0.0.1:15102
OBS_CASE=regression
OBS_PASS_COUNT=0 OBS_KNOWN_COUNT=0
mkdir -p /tmp/srs-obs-lib-regression
OBS_TEST_DIR=/tmp/srs-obs-lib-regression
OBS_STARTED=$SECONDS
OBS_API_WAIT=0
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check obs_test_assert_api H264 AAC
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"other","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":false},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":1280,"height":720},"audio":{"codec":"AAC"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"code":0,"streams":[{"app":"live","name":"test","publish":{"active":true},"video":{"codec":"H264","width":640,"height":360},"audio":{"codec":"Opus"}}]}'
check test "$(obs_test_assert_api H264 AAC >/dev/null 2>&1; echo $?)" = 1
fixture='{"streams":[{"codec_type":"video","codec_name":"h264","width":640,"height":360},{"codec_type":"audio","codec_name":"aac"}]}'
check obs_test_media_matches "$fixture" aac
fixture='{"streams":[{"codec_type":"video","codec_name":"h264","width":640,"height":360}]}'
check test "$(obs_test_media_matches "$fixture" aac; echo $?)" = 1
OBS_STARTED=$((SECONDS - 61))
check test "$(obs_test_budget >/dev/null 2>&1; echo $?)" = 1
OBS_STARTED=$SECONDS
check obs_test_budget
OBS_PASS_COUNT=0
OBS_KNOWN_COUNT=0
obs_test_known F2 'API frame count'
check test "$OBS_PASS_COUNT" = 0
check test "$OBS_KNOWN_COUNT" = 1
crashed_profile_retry() (
  OBS_WS=unused OBS_RECIPE=recipe OBS_CLIP=clip OBS_HTTP_URL=http://127.0.0.1:15101
  local calls=0
  obs_test_command() { calls=$((calls + 1)); [[ $calls -ne 1 ]] || return 2; }
  python3() { echo '{"obs_running":false,"live":{"connected":false}}'; }
  obs_test_setup --rtmp rtmp://127.0.0.1:15100/live/test || return 1
  [[ $calls -eq 3 ]]
)
check crashed_profile_retry
cleanup_failure() (
  OBS_WS=unused OBS_INSTANCE_ID=t12345 OBS_SRS_PID=
  python3() { return 1; }
  if obs_test_cleanup >/dev/null 2>&1; then return 1; fi
)
check cleanup_failure
# Cleanup must remove exactly the claimed instance and reap its owned SRS.
OBS_TEST_DIR=/tmp/srs-obs-lib-regression
OBS_WS=unused
OBS_INSTANCE_ID=t12345
python3() { printf '%s\n' "$*" > /tmp/srs-obs-lib-regression/remove.args; }
sleep 30 &
OBS_SRS_PID=$!
printf '%s\n' "$OBS_SRS_PID" > "$OBS_TEST_DIR/srs.pid"
obs_test_cleanup
check test "$(cat /tmp/srs-obs-lib-regression/remove.args)" = 'unused instance remove --instance t12345 --force --timeout 10'
check test "$(kill -0 "$OBS_SRS_PID" 2>/dev/null; echo $?)" != 0
check test ! -f /tmp/srs-obs-lib-regression/srs.pid
rm -rf /tmp/srs-obs-lib-regression
echo "PASS $checks harness regressions"
