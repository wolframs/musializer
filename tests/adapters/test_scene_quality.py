import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class SceneQualityRegressionTests(unittest.TestCase):
    def test_every_scene_exposes_renderer_backed_parameter_controls(self):
        scene_sources = [
            "scene_spectrum.c",
            "scene_pulse_field.c",
            "scene_orbital_lattice.c",
            "scene_ascii_field.c",
            "scene_song_atlas.c",
            "scene_spectral_terrarium.c",
            "scene_constellation.c",
        ]
        for filename in scene_sources:
            with self.subTest(scene=filename):
                source = (ROOT / "src" / filename).read_text(encoding="utf-8")
                self.assertIn("scene_settings_get(", source)

    def test_scene_inspector_resizes_only_when_monitor_space_allows(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("scene_settings_window_can_expand(", source)
        self.assertIn("GetMonitorWidth(monitor)", source)
        self.assertIn("SetWindowSize(p->scene_settings_expanded_width", source)
        self.assertIn("scene_settings_panel((Rectangle){", source)
        self.assertIn("workspace_width = settings_layout.workspace_width", source)
        self.assertIn('p->scene_settings_open ? "Hide" : "Tune"', source)

    def test_scene_settings_are_saved_and_restored_with_the_track(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("scene_settings_export_mappings(", source)
        self.assertIn("scene_settings_import_mappings(", source)
        self.assertIn("track->scene_settings = hydrated_settings", source)
        self.assertIn("mark_project_dirty(track);", source)

    def test_song_atlas_exposes_extended_tuning_and_real_wireframe_mode(self):
        settings = (ROOT / "src/scene_settings.c").read_text(encoding="utf-8")
        scene = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")
        ui = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn('"settings.atlas.height", "Terrain height", 0.35f, 2.75f', settings)
        self.assertIn('"settings.atlas.width", "Terrain width", 0.55f, 3.20f', settings)
        self.assertIn('"settings.atlas.camera", "Camera height", 0.25f, 1.75f', settings)
        self.assertIn('"settings.atlas.color", "Hue shift (deg)"', settings)
        self.assertIn('"settings.atlas.speed", "Camera speed"', settings)
        self.assertIn('TOGGLE("settings.atlas.wireframe"', settings)
        self.assertIn("ATLAS_SETTING_COLOR", scene)
        self.assertIn("ATLAS_SETTING_SPEED", scene)
        self.assertIn("ATLAS_SETTING_WIREFRAME", scene)
        self.assertGreaterEqual(scene.count("if (!wireframe)"), 2)
        self.assertIn('"Filled", !enabled', ui)
        self.assertIn('"Wireframe", enabled', ui)
        self.assertIn("p->scene_settings_scroll/max_scroll", ui)

    def test_ascii_field_animates_glyphs_and_uses_compression_safe_scanlines(self):
        source = (ROOT / "src/scene_ascii_field.c").read_text(encoding="utf-8")

        self.assertIn("ascii_art_animated_glyph(", source)
        self.assertIn("float glyph_wave = sinf(", source)
        self.assertIn("float counter_wave = sinf(", source)
        self.assertIn("fmaxf(2.0f*pixel_scale, 1.0f)", source)
        self.assertIn("42.0f + energy*10.0f", source)
        self.assertNotIn("(Color){0, 0, 0, 12}", source)

    def test_song_atlas_scrolls_between_fixed_history_samples(self):
        source = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")

        self.assertIn("#define ATLAS_CAPTURE_INTERVAL", source)
        self.assertIn("static float atlas_scroll_phase", source)
        self.assertIn("time_seconds - atlas->last_capture_time", source)
        capture_condition = re.search(
            r"if \(atlas->last_capture_time < 0\.0 \|\|(?P<body>.*?)\) \{\s*"
            r"atlas_capture\(atlas, frame\);",
            source,
            re.DOTALL,
        )
        self.assertIsNotNone(capture_condition)
        self.assertIn("ATLAS_CAPTURE_INTERVAL", capture_condition.group("body"))
        self.assertNotIn("frame->audio.onset", capture_condition.group("body"))

    def test_song_atlas_heightfield_faces_camera_and_batches_map_lines(self):
        source = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")

        upward_winding = re.compile(
            r"atlas_rl_vertex\(a, ca\);\s*atlas_rl_vertex\(b, cb\);\s*"
            r"atlas_rl_vertex\(c, cc\);\s*atlas_rl_vertex\(b, cb\);\s*"
            r"atlas_rl_vertex\(d, cd\);\s*atlas_rl_vertex\(c, cc\);"
        )
        self.assertRegex(source, upward_winding)
        # The whole-track path and live-input fallback each submit all contour
        # lines in one batch; neither regresses to a draw call per landmark.
        self.assertEqual(source.count("rlBegin(RL_LINES)"), 2)
        self.assertNotIn("scene_draw_tube", source)
        self.assertNotIn("DrawCube", source)

    def test_song_atlas_camera_focuses_within_remaining_map(self):
        source = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")

        self.assertIn("float remaining =", source)
        self.assertIn("remaining*0.45f", source)
        self.assertIn("target_z = 1.40f - focus_slices*ATLAS_SLICE_SPACING", source)
        self.assertNotIn(".target = { sinf(journey*0.31f)*0.34f, -0.78f, -9.8f }", source)


if __name__ == "__main__":
    unittest.main()
