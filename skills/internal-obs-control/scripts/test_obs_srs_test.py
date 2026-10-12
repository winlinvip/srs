import argparse
import unittest
from unittest.mock import Mock, patch

import obs_srs_test as recipe


class StreamRestartTest(unittest.TestCase):
    def run_action(self, action, active=True, stalled=False, changed=False, reconnecting=False):
        client = Mock()
        def call(name, *args):
            nonlocal active
            if name == "StopStream":
                active = False
            if name == "StartStream" and reconnecting:
                active = True
            if name == "GetStreamStatus":
                return {"outputActive": active}
            if name == "GetProfileList":
                return {"currentProfileName": "test", "profiles": ["other", "test"]}
            return {}
        client.call.side_effect = call
        args = argparse.Namespace(action=action, timeout=2)
        with patch.object(recipe.obsws, "connect", return_value=client), \
                patch.object(recipe.obsws, "dump"), \
                patch.object(recipe.time, "sleep") as sleep, \
                patch.object(recipe, "wait_stream") as wait, \
                patch.object(recipe, "stream_log_state", create=True) as logs:
            logs.side_effect = [0, 1 if changed else 0]
            wait.side_effect = ([{"outputActive": False}, recipe.obsws.TimeoutError_("stalled"),
                                 {"outputActive": True}] if stalled else
                                [{"outputActive": False}, {"outputActive": True}])
            recipe.cmd_stream(args)
        return client, sleep, wait

    def test_restart_stops_settles_and_starts(self):
        client, sleep, wait = self.run_action("restart")
        requests = [call.args[0] for call in client.call.call_args_list]
        self.assertIn("StopStream", requests)
        self.assertIn("StartStream", requests)
        self.assertLess(requests.index("StopStream"), requests.index("StartStream"))
        sleep.assert_called_once_with(1.0)
        self.assertEqual([call.kwargs["active"] for call in wait.call_args_list], [False, True])

    def test_start_inactive_waits_for_stop_to_settle(self):
        client, sleep, wait = self.run_action("start", active=False)
        sleep.assert_called_once_with(1.0)
        self.assertEqual([call.kwargs["active"] for call in wait.call_args_list], [False, True])

    def test_silent_stalled_restart_reloads_then_retries(self):
        client, _, _ = self.run_action("restart", stalled=True)
        requests = [call.args[0] for call in client.call.call_args_list]
        self.assertEqual(requests.count("StartStream"), 2)
        profiles = [call.args[1]["profileName"] for call in client.call.call_args_list
                    if call.args[0] == "SetCurrentProfile"]
        self.assertEqual(profiles, ["other", "test"])

    def test_failed_start_with_new_logs_is_not_retried(self):
        with self.assertRaises(recipe.obsws.TimeoutError_):
            self.run_action("restart", stalled=True, changed=True)

    def test_reconnecting_active_output_is_not_reloaded(self):
        with self.assertRaises(recipe.obsws.TimeoutError_):
            self.run_action("restart", stalled=True, reconnecting=True)


if __name__ == "__main__":
    unittest.main()
