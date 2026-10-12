import os
import io
import shutil
import unittest
from argparse import Namespace
from contextlib import redirect_stderr, redirect_stdout
from unittest.mock import patch

import obsws


class ProcessSafetyTest(unittest.TestCase):
    def launch(self, instance=None, keep=False):
        output = io.StringIO()
        with patch.object(obsws, "INSTANCE", instance), \
                patch.object(obsws, "obs_pids", return_value=[]), \
                patch.object(obsws, "all_obs_pids", return_value=[]), \
                patch.object(obsws, "find_obs_binary", return_value=("obs", None)), \
                patch.object(obsws, "stale_sentinels", return_value=["sentinel-one", "sentinel-two"]), \
                patch.object(obsws, "config_dir", return_value="config"), \
                patch.object(obsws, "seed_instance", return_value=15000), \
                patch.object(obsws, "instance_env", return_value={}), \
                patch.object(obsws, "build_launch_args", return_value=[]), \
                patch.object(obsws, "read_websocket_config", return_value={"server_enabled": True}), \
                patch.object(obsws, "system", return_value="Windows"), \
                patch.object(obsws.subprocess, "Popen"), \
                patch("builtins.open", unittest.mock.mock_open()), \
                patch.object(obsws.os, "remove") as remove, redirect_stdout(output):
            self.assertEqual(obsws.cmd_launch(Namespace(keep_sentinel=keep)), 0)
        return remove, output.getvalue()

    def test_main_launch_preserves_and_lists_crash_sentinels(self):
        remove, output = self.launch()
        remove.assert_not_called()
        self.assertIn("sentinel-one", output)
        self.assertIn("sentinel-two", output)
        self.assertIn("crash", output)
        self.assertIn("remove", output)

    def test_instance_launch_clears_only_its_sentinels(self):
        # Accepted behavior: isolated test instances clear their own recovery state.
        remove, output = self.launch(instance="t12345")
        self.assertEqual(remove.call_args_list,
                         [unittest.mock.call("sentinel-one"), unittest.mock.call("sentinel-two")])
        self.assertIn("removed 2", output)

    def test_keep_sentinel_preserves_instance_recovery_state(self):
        # Accepted behavior: the existing opt-out also applies to test instances.
        remove, _ = self.launch(instance="t12345", keep=True)
        remove.assert_not_called()

    def test_windows_quit_addresses_each_selected_pid(self):
        with patch.object(obsws, "system", return_value="Windows"), \
                patch.object(obsws, "obs_pids", return_value=[123, 456]), \
                patch.object(obsws, "connect", side_effect=obsws.OBSError("offline")), \
                patch.object(obsws, "pid_is_obs", return_value=False), \
                patch.object(obsws.subprocess, "run") as run, redirect_stdout(io.StringIO()):
            self.assertEqual(obsws.cmd_quit(Namespace(timeout=1)), 0)
        self.assertEqual(run.call_args_list, [
            unittest.mock.call(["taskkill", "/PID", "123"], capture_output=True, timeout=15),
            unittest.mock.call(["taskkill", "/PID", "456"], capture_output=True, timeout=15)])


class DisplayPropertiesTest(unittest.TestCase):
    def test_display_uuid_refused_before_connection(self):
        error = io.StringIO()
        with patch.object(obsws, "connect") as connect, redirect_stderr(error):
            connect.return_value.request.return_value = {"requestStatus": {"result": False}}
            result = obsws.main(["call", "GetInputPropertiesListPropertyItems",
                                 '{"inputName":"screen","propertyName":"display_uuid"}'])
        self.assertEqual(result, 1)
        connect.assert_not_called()
        self.assertIn("CoreGraphics", error.getvalue())
        self.assertIn("--force", error.getvalue())

    def test_force_sends_display_uuid_request(self):
        with patch.object(obsws, "connect") as connect, redirect_stdout(io.StringIO()):
            connect.return_value.request.return_value = {"requestStatus": {"result": True}}
            result = obsws.main(["call", "--force", "GetInputPropertiesListPropertyItems",
                                 '{"inputName":"screen","propertyName":"display_uuid"}'])
        self.assertEqual(result, 0)
        connect.return_value.request.assert_called_once_with(
            "GetInputPropertiesListPropertyItems", {"inputName": "screen", "propertyName": "display_uuid"})
        connect.return_value.close.assert_called_once_with()

    def test_other_properties_and_requests_still_work(self):
        # Accepted behavior: only the known dangerous request/property pair is guarded.
        for request, data in (("GetInputPropertiesListPropertyItems", '{"propertyName":"device"}'),
                              ("GetInputSettings", '{"propertyName":"display_uuid"}'),
                              ("GetVersion", None)):
            with self.subTest(request=request), patch.object(obsws, "connect") as connect, redirect_stdout(io.StringIO()):
                connect.return_value.request.return_value = {"requestStatus": {"result": True}}
                self.assertEqual(obsws.main(["call", request] + ([data] if data else [])), 0)
                connect.return_value.request.assert_called_once()


class InstancePortsTest(unittest.TestCase):
    def setUp(self):
        os.mkdir("/tmp/srs-obs-ports-unit")
        self.addCleanup(shutil.rmtree, "/tmp/srs-obs-ports-unit")
        self.env = patch.dict(os.environ, {"OBS_CONTROL_HOME": "/tmp/srs-obs-ports-unit"}, clear=True)
        self.env.start()
        self.addCleanup(self.env.stop)
        self.main = patch.object(obsws, "main_config_dir", return_value="/tmp/srs-obs-ports-unit/main")
        self.main.start()
        self.addCleanup(self.main.stop)
        self.free = patch.object(obsws, "port_free", return_value=True)
        self.free.start()
        self.addCleanup(self.free.stop)

    def test_default_range(self):
        # Accepted compatibility behavior: unset preserves the human-use defaults.
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 4460)

    def test_custom_range_allocates_distinct_ports_under_lock(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15099"
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15000)
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_exhaustion_names_requested_range(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15000"
        obsws.seed_instance(obsws.new_instance())
        with self.assertRaisesRegex(obsws.LocalStateError, "no free websocket port in 15000-15000"):
            obsws.seed_instance(obsws.new_instance())

    def test_skips_occupied_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        with patch.object(obsws, "port_free", side_effect=lambda port: port != 15000):
            self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_skips_main_obs_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        obsws.write_websocket_config({"server_port": 15000}, obsws.main_config_dir())
        self.assertEqual(obsws.seed_instance(obsws.new_instance()), 15001)

    def test_relaunch_preserves_existing_port(self):
        os.environ["OBS_CONTROL_WS_PORTS"] = "15000-15002"
        instance = obsws.new_instance()
        self.assertEqual(obsws.seed_instance(instance), 15000)
        os.environ["OBS_CONTROL_WS_PORTS"] = "15100-15102"
        self.assertEqual(obsws.seed_instance(instance), 15000)

    def test_invalid_ranges_fail_clearly(self):
        for value in ("", "15000", "x-y", "15002-15000", "0-1", "65535-65536", "-1-2"):
            with self.subTest(value=value):
                os.environ["OBS_CONTROL_WS_PORTS"] = value
                with self.assertRaisesRegex(obsws.LocalStateError, "OBS_CONTROL_WS_PORTS"):
                    obsws.seed_instance(obsws.new_instance())


if __name__ == "__main__":
    unittest.main()
