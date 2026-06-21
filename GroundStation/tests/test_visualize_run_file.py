import math
import importlib.util
import tempfile
import unittest
from pathlib import Path

from GroundStation.VisualizeRunFile import (
    DRAW_LINE_WIDTH,
    GO_HOME_S0_DEG,
    GO_HOME_S1_DEG,
    IntentError,
    ParsedCommand,
    TRAVEL_LINE_WIDTH,
    Vec2,
    ang_to_cart,
    cart_to_ang,
    home_position_m,
    parse_program,
    plot_simulation,
    save_animation_gif,
    simulate_commands,
    simulate_file,
)


class VisualizeRunFileTests(unittest.TestCase):
    def test_kinematics_round_trip(self):
        expected = ang_to_cart(35.0, -70.0)
        s0_deg, s1_deg = cart_to_ang(expected)
        actual = ang_to_cart(s0_deg, s1_deg)

        self.assertAlmostEqual(actual.x, expected.x, places=5)
        self.assertAlmostEqual(actual.y, expected.y, places=5)

    def test_home_position_uses_firmware_go_home_angles(self):
        expected = ang_to_cart(GO_HOME_S0_DEG, GO_HOME_S1_DEG)
        actual = home_position_m()

        self.assertAlmostEqual(actual.x, expected.x, places=7)
        self.assertAlmostEqual(actual.y, expected.y, places=7)

    def test_parse_program_accepts_legacy_aliases(self):
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / "program.txt"
            program.write_text(
                "\n".join([
                    "SetMotorLimits motor=All accel=100 speed=200",
                    "CNC_Jog TargetX_m=0.10 TargetY_m=0.20 LinearSpeed_mps=0.03 PumpOn=1",
                ]),
                encoding="utf-8",
            )

            commands = parse_program(program)

        self.assertEqual([command.cmd for command in commands], ["set_motor_limits", "cnc_jog"])
        self.assertEqual(commands[1].args["PumpOn"], 1)

    def test_parse_smiley_face_program(self):
        commands = parse_program(Path("GroundStation/GCode/SmileyFace.txt"))
        motion_commands = [
            command.cmd for command in commands
            if command.cmd in {"cnc_jog", "cnc_arc", "cnc_spiral", "cnc_rectangle", "cnc_go_to_angle", "cnc_go_home", "wait"}
        ]

        self.assertGreaterEqual(len(commands), 8)
        self.assertIn("cnc_spiral", [command.cmd for command in commands])
        self.assertEqual(motion_commands[0], "cnc_jog")

    def test_parse_program_expands_nested_run_file_and_local_origin(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            child = root / "child.txt"
            parent = root / "parent.txt"
            child.write_text(
                "cnc_jog TargetX_m=0.01 TargetY_m=0.02 LinearSpeed_mps=0.03 PumpOn=0\n",
                encoding="utf-8",
            )
            parent.write_text(
                "local_origin OriginX_m=0.10 OriginY_m=0.20\n"
                "run_file child.txt\n",
                encoding="utf-8",
            )

            commands = parse_program(parent)

        self.assertEqual([command.cmd for command in commands], ["local_origin", "cnc_jog"])
        self.assertAlmostEqual(commands[1].args["TargetX_m"], 0.11)
        self.assertAlmostEqual(commands[1].args["TargetY_m"], 0.22)

    def test_parse_program_disallows_recursive_run_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            recursive = Path(tmp) / "recursive.txt"
            recursive.write_text("run_file recursive.txt\n", encoding="utf-8")

            with self.assertRaisesRegex(IntentError, "recursive run_file include disallowed"):
                parse_program(recursive)

    def test_simulate_multi_smile_run_file(self):
        result = simulate_file(Path("GroundStation/GCode/multi_smile.txt"), dt_ms=100)

        draw_segments = [segment for segment in result.segments if segment.pump_on]

        self.assertGreaterEqual(len(draw_segments), 4)
        self.assertTrue(math.isfinite(result.final_position_m.x))
        self.assertTrue(math.isfinite(result.final_position_m.y))

    def test_simulation_classifies_pump_on_and_off_segments(self):
        commands = [
            _command("set_motor_limits", {"motor": "All", "accel": 10000.0, "speed": 10000.0}),
            _command("set_accel_scale", {"accelScale": 1.0}),
            _command("cnc_jog", {
                "TargetX_m": 0.20,
                "TargetY_m": 0.03,
                "LinearSpeed_mps": 0.10,
                "PumpOn": 0,
            }),
            _command("cnc_jog", {
                "TargetX_m": 0.19,
                "TargetY_m": 0.04,
                "LinearSpeed_mps": 0.10,
                "PumpOn": 1,
            }),
        ]

        result = simulate_commands(commands, max_command_seconds=10.0)

        self.assertEqual([segment.pump_on for segment in result.simulated_segments], [False, True])
        self.assertEqual(TRAVEL_LINE_WIDTH, 0.8)
        self.assertEqual(DRAW_LINE_WIDTH, 3.0)

    def test_simple_jog_requested_path_reaches_endpoint(self):
        endpoint = Vec2(0.20, 0.03)
        commands = [
            _command("set_motor_limits", {"motor": "All", "accel": 10000.0, "speed": 10000.0}),
            _command("set_accel_scale", {"accelScale": 1.0}),
            _command("cnc_jog", {
                "TargetX_m": endpoint.x,
                "TargetY_m": endpoint.y,
                "LinearSpeed_mps": 0.10,
                "PumpOn": 0,
            }),
        ]

        result = simulate_commands(commands, max_command_seconds=10.0)
        final_requested = result.requested_segments[-1].points[-1]

        self.assertAlmostEqual(final_requested.x, endpoint.x, places=6)
        self.assertAlmostEqual(final_requested.y, endpoint.y, places=6)
        self.assertTrue(math.isfinite(result.final_position_m.x))
        self.assertTrue(math.isfinite(result.final_position_m.y))

    def test_simulation_records_timeline_durations_for_animation(self):
        start = Vec2(0.10, 0.20)
        endpoint = Vec2(0.13, 0.20)
        start_s0_deg, start_s1_deg = cart_to_ang(start)
        commands = [
            _command("cnc_jog", {
                "TargetX_m": endpoint.x,
                "TargetY_m": endpoint.y,
                "LinearSpeed_mps": 0.03,
                "PumpOn": 1,
            }),
            _command("wait", {"timeout_ms": 500}),
        ]

        result = simulate_commands(
            commands,
            dt_ms=100,
            start_s0_deg=start_s0_deg,
            start_s1_deg=start_s1_deg,
        )

        self.assertEqual([step.command for step in result.timeline], ["cnc_jog", "wait"])
        self.assertEqual(result.timeline[0].duration_ms, 1000)
        self.assertEqual(result.timeline[1].duration_ms, 500)
        self.assertTrue(result.timeline[0].pump_on)

    @unittest.skipUnless(importlib.util.find_spec("matplotlib"), "matplotlib is not installed")
    def test_plot_simulation_writes_png_with_headless_backend(self):
        import matplotlib

        matplotlib.use("Agg")
        commands = [
            _command("set_motor_limits", {"motor": "All", "accel": 10000.0, "speed": 10000.0}),
            _command("set_accel_scale", {"accelScale": 1.0}),
            _command("cnc_jog", {
                "TargetX_m": 0.20,
                "TargetY_m": 0.03,
                "LinearSpeed_mps": 0.10,
                "PumpOn": 0,
            }),
        ]
        result = simulate_commands(commands, max_command_seconds=10.0)

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "preview.png"
            plot_simulation(result, output=output, show=False)

            self.assertTrue(output.exists())
            self.assertGreater(output.stat().st_size, 0)

    @unittest.skipUnless(
        importlib.util.find_spec("matplotlib") and importlib.util.find_spec("PIL"),
        "matplotlib and pillow are required for GIF output",
    )
    def test_save_animation_gif_writes_gif_with_headless_backend(self):
        import matplotlib

        matplotlib.use("Agg")
        start = Vec2(0.10, 0.20)
        endpoint = Vec2(0.11, 0.20)
        start_s0_deg, start_s1_deg = cart_to_ang(start)
        commands = [
            _command("cnc_jog", {
                "TargetX_m": endpoint.x,
                "TargetY_m": endpoint.y,
                "LinearSpeed_mps": 0.10,
                "PumpOn": 0,
            }),
            _command("wait", {"timeout_ms": 100}),
        ]
        result = simulate_commands(
            commands,
            dt_ms=50,
            start_s0_deg=start_s0_deg,
            start_s1_deg=start_s1_deg,
        )

        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "preview.gif"
            save_animation_gif(result, output=output, fps=5.0)

            self.assertTrue(output.exists())
            self.assertGreater(output.stat().st_size, 0)
            self.assertIn(output.read_bytes()[:6], {b"GIF87a", b"GIF89a"})


def _command(cmd, args):
    return ParsedCommand(line_no=1, raw=cmd, cmd=cmd, args=args)


if __name__ == "__main__":
    unittest.main()
