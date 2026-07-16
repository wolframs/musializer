import json
import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class SceneQualityRegressionTests(unittest.TestCase):
    def test_scene_reset_undo_is_scoped_to_track_and_scene(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        guard = source.index("if (p->scene_settings_reset_scene != p->scene.id ||")
        panel = source.index("float button_y =", guard)
        self.assertIn(
            "p->scene_settings_reset_track != p->current_track",
            source[guard:panel],
        )
        track_switch = source.index("p->current_track = i;")
        track_switch_end = source.index("}", track_switch)
        self.assertIn(
            "p->scene_settings_reset_undo_available = false;",
            source[track_switch:track_switch_end],
        )

    def test_assist_rejection_detail_is_shared_by_panel_and_notice(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        helper = source.index("static void assist_candidate_failure(")
        loader = source.index("static Analysis_Candidate *load_analysis_candidate", helper)
        helper_source = source[helper:loader]
        self.assertIn("p->assist_failure_detail", helper_source)
        self.assertIn(
            "notice_push(UI_NOTICE_ERROR, title, p->assist_failure_detail, path, true)",
            helper_source,
        )

    def test_project_preset_capacity_matches_every_scene_slot(self):
        schema = json.loads(
            (ROOT / "schemas/project-v1.schema.json").read_text(encoding="utf-8")
        )
        project_header = (ROOT / "src/project.h").read_text(encoding="utf-8")

        expected = 10 * 8
        self.assertEqual(schema["properties"]["scene_presets"]["maxItems"], expected)
        self.assertIn(
            "(SCENE_SETTINGS_SCENE_COUNT*SCENE_SETTINGS_PRESETS_PER_SCENE)",
            project_header,
        )

    def test_every_scene_exposes_renderer_backed_parameter_controls(self):
        scene_sources = [
            "scene_spectrum.c",
            "scene_pulse_field.c",
            "scene_orbital_lattice.c",
            "scene_ascii_field.c",
            "scene_song_atlas.c",
            "scene_spectral_terrarium.c",
            "scene_constellation.c",
            "scene_cadence.c",
            "scene_loom.c",
            "scene_pentagram.c",
        ]
        for filename in scene_sources:
            with self.subTest(scene=filename):
                source = (ROOT / "src" / filename).read_text(encoding="utf-8")
                self.assertIn("scene_settings_get(", source)

    def test_scene_inspector_resizes_only_after_explicit_expand(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("scene_settings_window_can_expand(", source)
        self.assertIn("GetMonitorWidth(monitor)", source)
        self.assertIn("SetWindowSize(p->scene_settings_expanded_width", source)
        open_start = source.index("static void set_scene_settings_open(bool open)")
        close_branch = source.index("p->scene_settings_open = false;", open_start)
        self.assertNotIn("SetWindowSize(", source[open_start:close_branch])
        self.assertIn('"Expand"', source)
        self.assertIn("scene_settings_panel((Rectangle){", source)
        self.assertIn("workspace_width = settings_layout.workspace_width", source)
        self.assertIn('p->scene_settings_open ? "Hide" : "Tune"', source)

    def test_scene_settings_are_saved_and_restored_with_the_track(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("scene_settings_export_mappings(", source)
        self.assertIn("scene_settings_import_mappings(", source)
        self.assertIn("track->scene_settings = hydrated_settings", source)
        self.assertIn("mark_project_dirty(track);", source)

    def test_scene_presets_and_cues_capture_durable_tuning_snapshots(self):
        source = (ROOT / "src/plug.c").read_text(encoding="utf-8")
        codec = (ROOT / "src/project_io.c").read_text(encoding="utf-8")

        self.assertIn('"+ Scene"', source)
        self.assertIn("scene_switch_cue_at(", source)
        self.assertIn("scene_settings_capture(", source)
        self.assertIn("track_effective_scene_settings(track)", source)
        self.assertIn("scene_settings_preset_save(", source)
        self.assertIn("scene_settings_preset_apply(", source)
        self.assertIn('\\"scene_presets\\"', codec)

    def test_song_atlas_exposes_extended_tuning_and_real_wireframe_mode(self):
        settings = (ROOT / "src/scene_settings.c").read_text(encoding="utf-8")
        scene = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")
        ui = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn('"settings.atlas.height", "Terrain height", 0.35f, 2.75f', settings)
        self.assertIn('"settings.atlas.width", "Terrain width", 0.55f, 3.20f', settings)
        self.assertIn('"settings.atlas.camera", "Camera height", 0.25f, 1.75f', settings)
        self.assertIn('"settings.atlas.color", "Hue shift (deg)"', settings)
        self.assertIn('"settings.atlas.speed", "Camera drift"', settings)
        self.assertIn('TOGGLE("settings.atlas.wireframe"', settings)
        self.assertIn('"settings.atlas.detail", "Sampling detail"', settings)
        self.assertIn('TOGGLE("settings.atlas.hue_motion", "Hue motion"', settings)
        self.assertIn('"settings.atlas.orbit", "Camera orbit"', settings)
        self.assertIn('"settings.atlas.zoom", "Camera distance"', settings)
        self.assertIn("ATLAS_SETTING_COLOR", scene)
        self.assertIn("ATLAS_SETTING_SPEED", scene)
        self.assertIn("ATLAS_SETTING_WIREFRAME", scene)
        self.assertIn("ATLAS_SETTING_DETAIL", scene)
        self.assertIn("ATLAS_SETTING_HUE_MOTION", scene)
        self.assertIn("ATLAS_SETTING_ORBIT", scene)
        self.assertIn("ATLAS_SETTING_ZOOM", scene)
        self.assertIn("song_atlas_map_render_sample_count(", scene)
        self.assertIn("atlas_map_dynamics(", scene)
        self.assertGreaterEqual(scene.count("if (!wireframe)"), 2)
        self.assertIn('hue_motion ? "Manual" : "Filled"', ui)
        self.assertIn('hue_motion ? "Music" : "Wireframe"', ui)
        self.assertIn("p->scene_settings_scroll/max_scroll", ui)

    def test_ascii_field_animates_glyphs_and_uses_compression_safe_scanlines(self):
        source = (ROOT / "src/scene_ascii_field.c").read_text(encoding="utf-8")

        self.assertIn("ascii_art_animated_glyph(", source)
        self.assertIn("float glyph_wave = sinf(", source)
        self.assertIn("float counter_wave = sinf(", source)
        self.assertIn("fmaxf(2.0f*pixel_scale, 1.0f)", source)
        self.assertIn("42.0f + energy*10.0f", source)
        self.assertNotIn("(Color){0, 0, 0, 12}", source)

    def test_cadence_word_capacity_covers_the_full_lyric_contract(self):
        source = (ROOT / "src/scene_cadence.c").read_text(encoding="utf-8")

        self.assertIn(
            "CADENCE_MAX_WORDS = (LYRICS_TEXT_CAPACITY + 1U)/2U",
            source,
        )
        self.assertNotIn("CADENCE_MAX_WORDS = 32", source)

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

    def test_song_atlas_live_detail_preserves_depth_while_adding_samples(self):
        source = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")

        self.assertIn("ATLAS_BASE_CAPTURE_INTERVAL", source)
        self.assertIn("song_atlas_map_render_sample_count(", source)
        self.assertIn("song_atlas_map_render_sample_index(", source)
        self.assertIn("song_atlas_map_render_distance(source_age + scroll_phase)", source)

    def test_constellation_rebases_its_motion_envelope_on_seeks(self):
        source = (ROOT / "src/scene_constellation.c").read_text(encoding="utf-8")
        motion = (ROOT / "src/scene_constellation_motion.c").read_text(
            encoding="utf-8"
        )

        self.assertIn("constellation_motion_update(", source)
        self.assertIn("bool discontinuity", motion)
        self.assertIn("constellation_motion_rebase(motion, input)", motion)

    def test_song_atlas_heightfield_faces_camera_and_batches_map_lines(self):
        source = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")

        self.assertGreaterEqual(source.count("atlas_lit_triangle(a, b, c"), 2)
        self.assertGreaterEqual(source.count("atlas_lit_triangle(b, d, c"), 2)
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

    def test_creative_scene_mechanisms_are_renderer_backed(self):
        pulse = (ROOT / "src/scene_pulse_field.c").read_text(encoding="utf-8")
        ascii_scene = (ROOT / "src/scene_ascii_field.c").read_text(encoding="utf-8")
        orbital = (ROOT / "src/scene_orbital_lattice.c").read_text(encoding="utf-8")
        atlas = (ROOT / "src/scene_song_atlas.c").read_text(encoding="utf-8")
        terrarium = (ROOT / "src/scene_spectral_terrarium.c").read_text(encoding="utf-8")
        constellation = (ROOT / "src/scene_constellation.c").read_text(encoding="utf-8")
        cadence = (ROOT / "src/scene_cadence.c").read_text(encoding="utf-8")
        loom = (ROOT / "src/scene_loom.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        self.assertIn("cosf((float)fold*theta", pulse)
        self.assertIn("spectrum_history", ascii_scene)
        self.assertIn("orbital_draw_swaying_link", orbital)
        self.assertIn("atlas_triangle_light", atlas)
        self.assertIn("neighbors += 1", terrarium)
        self.assertIn("BeginBlendMode(BLEND_ADDITIVE)", constellation)
        self.assertIn("cadence_split_words", cadence)
        self.assertIn("frame->audio.beat_phase", cadence)
        self.assertIn("semantic_lane_sample", loom)
        self.assertIn("frame->duration_seconds", loom)
        self.assertIn("p->scene.id != SCENE_CADENCE", plug)
        self.assertIn("fminf(216.0f, sidebar_height)", plug)

    def test_signature_scenes_are_registered_everywhere(self):
        scene_header = (ROOT / "src/scene.h").read_text(encoding="utf-8")
        registry = (ROOT / "src/scene.c").read_text(encoding="utf-8")
        settings = (ROOT / "src/scene_settings.c").read_text(encoding="utf-8")
        build = (ROOT / "src_build/nob_stage2.c").read_text(encoding="utf-8")
        plug = (ROOT / "src/plug.c").read_text(encoding="utf-8")

        for upper, stable in (("CADENCE", "cadence"), ("LOOM", "loom")):
            self.assertIn(f"SCENE_{upper}", scene_header)
            self.assertIn(f"scene_{stable}_descriptor", registry)
            self.assertIn(f"settings.{stable}.", settings)
            self.assertIn(f'"./src/scene_{stable}.c"', build)
            self.assertIn(f'case SCENE_{upper}: return "{stable}"', plug)


if __name__ == "__main__":
    unittest.main()
