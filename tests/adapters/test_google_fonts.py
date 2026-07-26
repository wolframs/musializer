from __future__ import annotations

import hashlib
import json
import os
import sys
import tempfile
import unittest
import urllib.error
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import google_fonts  # noqa: E402


TRUETYPE = b"\x00\x01\x00\x00" + b"pretend sfnt payload"
LICENCE = b"Copyright 2016 The Example Project Authors\nSIL Open Font License 1.1\n"
CSS = (
    "@font-face {\n"
    "  font-family: 'Example Sans';\n"
    "  font-style: normal;\n"
    "  font-weight: 400;\n"
    "  src: url(https://fonts.gstatic.com/s/examplesans/v1/abc.ttf)"
    " format('truetype');\n"
    "}\n"
)


class FakeResponse:
    def __init__(self, payload: bytes, url: str):
        self._payload = payload
        self.url = url

    def read(self, size: int | None = None) -> bytes:
        return self._payload if size is None else self._payload[:size]

    def __enter__(self):
        return self

    def __exit__(self, *_exc):
        return False


def routed(routes: dict[str, bytes], *, log: list[str] | None = None, final_url=None):
    """A transport that answers from a table and records what was asked for."""

    def transport(request, timeout=None):
        url = request.full_url
        if log is not None:
            log.append(url)
        for prefix, payload in routes.items():
            if url.startswith(prefix):
                return FakeResponse(payload, final_url(url) if final_url else url)
        raise urllib.error.HTTPError(url, 404, "Not Found", None, None)

    return transport


def working_routes():
    return {
        "https://fonts.googleapis.com/css2": CSS.encode("utf-8"),
        "https://fonts.gstatic.com/": TRUETYPE,
        "https://raw.githubusercontent.com/google/fonts/main/ofl/examplesans/OFL.txt": LICENCE,
    }


class HostBoundaryTests(unittest.TestCase):
    def test_only_declared_hosts_over_https_are_contacted(self):
        for allowed in (
            "https://fonts.google.com/metadata/fonts",
            "https://fonts.googleapis.com/css2?family=Inter",
            "https://fonts.gstatic.com/s/inter/v20/a.ttf",
            "https://raw.githubusercontent.com/google/fonts/main/ofl/inter/OFL.txt",
        ):
            self.assertEqual(google_fonts.check_host(allowed), allowed)

        for refused in (
            "http://fonts.googleapis.com/css2",          # plaintext
            "https://fonts.googleapis.com.evil.test/x",  # suffix, not the host
            "https://evil.test/fonts.googleapis.com",    # host in the path
            "https://openrouter.ai/api/v1/x",            # a boundary we do not share
            "file:///etc/passwd",
        ):
            with self.assertRaises(google_fonts.FontFetchError):
                google_fonts.check_host(refused)

    def test_a_redirect_off_the_boundary_is_refused_after_the_fact(self):
        # The first request is in bounds; the response says it came from
        # somewhere else. Following that quietly would make the declared
        # boundary a description of intent rather than of behaviour.
        transport = routed(
            {"https://fonts.googleapis.com/css2": CSS.encode("utf-8")},
            final_url=lambda _url: "https://evil.test/redirected",
        )
        with self.assertRaises(google_fonts.FontFetchError):
            google_fonts.resolve_truetype_url("Example Sans", transport=transport)

    def test_an_oversized_response_is_refused_rather_than_truncated(self):
        payload = b"x" * (google_fonts.LICENCE_BYTE_LIMIT + 1)
        transport = routed({"https://fonts.googleapis.com/css2": payload})
        with self.assertRaises(google_fonts.FontFetchError) as raised:
            google_fonts.read_url(
                "https://fonts.googleapis.com/css2",
                byte_limit=google_fonts.LICENCE_BYTE_LIMIT,
                transport=transport,
            )
        self.assertIn("larger than", str(raised.exception))

        # Exactly at the limit is a valid response, not an off-by-one failure.
        exact = b"x" * google_fonts.LICENCE_BYTE_LIMIT
        self.assertEqual(
            google_fonts.read_url(
                "https://fonts.googleapis.com/css2",
                byte_limit=google_fonts.LICENCE_BYTE_LIMIT,
                transport=routed({"https://fonts.googleapis.com/css2": exact}),
            ),
            exact,
        )


class FetchTests(unittest.TestCase):
    def setUp(self):
        self._temporary = tempfile.TemporaryDirectory()
        self.output = Path(self._temporary.name) / "download"
        self.addCleanup(self._temporary.cleanup)

    def test_fetch_writes_face_licence_and_a_manifest_that_describes_them(self):
        log: list[str] = []
        manifest = google_fonts.fetch_family(
            "Example Sans", self.output, transport=routed(working_routes(), log=log)
        )

        self.assertEqual(manifest["family"], "Example Sans")
        self.assertEqual(manifest["licence_name"], "OFL-1.1")
        self.assertEqual(manifest["font_bytes"], len(TRUETYPE))
        self.assertEqual(manifest["font_sha256"], hashlib.sha256(TRUETYPE).hexdigest())
        self.assertEqual(manifest["licence_sha256"], hashlib.sha256(LICENCE).hexdigest())

        # The digests in the manifest are what the application verifies before
        # the bytes reach a rasteriser, so they must describe the files on disk.
        face = Path(manifest["font_path"])
        licence = Path(manifest["licence_path"])
        self.assertEqual(face.read_bytes(), TRUETYPE)
        self.assertEqual(licence.read_bytes(), LICENCE)
        self.assertEqual(
            hashlib.sha256(face.read_bytes()).hexdigest(), manifest["font_sha256"]
        )
        self.assertEqual(
            hashlib.sha256(licence.read_bytes()).hexdigest(), manifest["licence_sha256"]
        )
        on_disk = json.loads(
            (self.output / "examplesans.manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(on_disk, manifest)

        # Only the family name is ever sent, and never as user content.
        self.assertTrue(any("family=Example+Sans" in url for url in log))

    def test_a_response_that_is_not_a_font_never_reaches_the_disk(self):
        # A captive portal answering 200 with a login page is the realistic
        # shape of this: the request succeeds and the payload is HTML.
        routes = working_routes()
        routes["https://fonts.gstatic.com/"] = b"<!doctype html><title>Sign in</title>"
        with self.assertRaises(google_fonts.FontFetchError) as raised:
            google_fonts.fetch_family(
                "Example Sans", self.output, transport=routed(routes)
            )
        self.assertIn("not a font", str(raised.exception))
        self.assertFalse(self.output.exists())

    def test_a_face_whose_licence_cannot_be_retrieved_is_refused(self):
        routes = working_routes()
        del routes["https://raw.githubusercontent.com/google/fonts/main/ofl/examplesans/OFL.txt"]
        with self.assertRaises(google_fonts.FontFetchError) as raised:
            google_fonts.fetch_family(
                "Example Sans", self.output, transport=routed(routes)
            )
        self.assertIn("licence", str(raised.exception))
        self.assertFalse(self.output.exists())

    def test_the_licence_directory_identifies_which_terms_apply(self):
        routes = working_routes()
        del routes["https://raw.githubusercontent.com/google/fonts/main/ofl/examplesans/OFL.txt"]
        routes[
            "https://raw.githubusercontent.com/google/fonts/main/apache/examplesans/LICENSE.txt"
        ] = b"Apache License, Version 2.0\n"
        manifest = google_fonts.fetch_family(
            "Example Sans", self.output, transport=routed(routes)
        )
        self.assertEqual(manifest["licence_name"], "Apache-2.0")

    def test_only_a_truetype_source_is_accepted_from_the_stylesheet(self):
        # woff2 downloads fine and then fails to rasterize, because raylib has
        # no decompressor for it. Refusing here names the real reason.
        woff2 = (
            "@font-face { src: url(https://fonts.gstatic.com/s/x/v1/a.woff2)"
            " format('woff2'); }"
        )
        transport = routed({"https://fonts.googleapis.com/css2": woff2.encode("utf-8")})
        with self.assertRaises(google_fonts.FontFetchError) as raised:
            google_fonts.resolve_truetype_url("Example Sans", transport=transport)
        self.assertIn("TrueType", str(raised.exception))

    def test_a_family_name_is_bounded_before_it_becomes_a_request(self):
        for refused in (
            "",
            "../../etc/passwd",
            "Example Sans\nInjected: header",
            "Example/Sans",
            "x" * 200,
        ):
            with self.assertRaises(google_fonts.FontFetchError):
                google_fonts.validate_family(refused)
        self.assertEqual(google_fonts.validate_family("  Space Mono  "), "Space Mono")
        self.assertEqual(google_fonts.family_directory("EB Garamond"), "ebgaramond")


class CatalogueTests(unittest.TestCase):
    def setUp(self):
        self._temporary = tempfile.TemporaryDirectory()
        self.cache = Path(self._temporary.name) / "catalogue.json"
        self.addCleanup(self._temporary.cleanup)

    @staticmethod
    def raw(entries):
        return json.dumps({"familyMetadataList": entries}).encode("utf-8")

    def test_reduce_keeps_latin_families_and_drops_what_cannot_be_typeset(self):
        payload = json.loads(
            self.raw(
                [
                    {"family": "Beta", "category": "Serif",
                     "subsets": ["menu", "latin"], "popularity": 7},
                    {"family": "Alpha", "category": "Sans Serif",
                     "subsets": ["latin", "greek"], "popularity": 2},
                    # No Latin subset: it would download and render empty boxes.
                    {"family": "Noto Tamil", "category": "Sans Serif",
                     "subsets": ["tamil"], "popularity": 1},
                    {"family": "Bad/Name", "subsets": ["latin"]},
                    "not an object",
                ]
            ).decode("utf-8")
        )
        reduced = google_fonts.reduce_catalogue(payload)
        self.assertEqual(reduced["family_count"], 2)
        self.assertEqual([f["family"] for f in reduced["families"]], ["Alpha", "Beta"])
        # "menu" is Google's internal preview subset, not a script a user picks.
        self.assertEqual(reduced["families"][0]["subsets"], ["greek", "latin"])

    def test_an_empty_or_malformed_catalogue_is_rejected_not_cached(self):
        for payload in ({}, {"familyMetadataList": []}, {"familyMetadataList": {}},
                        {"familyMetadataList": [{"family": "Noto Tamil",
                                                 "subsets": ["tamil"]}]}):
            with self.assertRaises(ValueError):
                google_fonts.reduce_catalogue(payload)
        self.assertFalse(self.cache.exists())

    def test_a_fresh_cache_is_reused_and_a_stale_one_is_refetched(self):
        entries = [{"family": "Alpha", "category": "Serif",
                    "subsets": ["latin"], "popularity": 1}]
        log: list[str] = []
        transport = routed({"https://fonts.google.com/metadata/fonts": self.raw(entries)},
                           log=log)

        first = google_fonts.load_catalogue(self.cache, transport=transport)
        self.assertEqual(first["family_count"], 1)
        self.assertEqual(len(log), 1)

        google_fonts.load_catalogue(
            self.cache, transport=transport, max_age_seconds=10**9
        )
        self.assertEqual(len(log), 1, "a fresh cache must not open a connection")

        google_fonts.load_catalogue(
            self.cache, transport=transport, max_age_seconds=0.0
        )
        self.assertEqual(len(log), 2, "a stale cache must be refetched")

        google_fonts.load_catalogue(
            self.cache, transport=transport, max_age_seconds=10**9, force=True
        )
        self.assertEqual(len(log), 3, "--force must ignore a fresh cache")

    def test_freshness_is_measured_against_the_cache_file_mtime(self):
        self.cache.write_text(
            json.dumps({"schema_version": google_fonts.CATALOGUE_SCHEMA_VERSION}),
            encoding="utf-8",
        )
        os.utime(self.cache, (1000.0, 1000.0))
        self.assertTrue(
            google_fonts.catalogue_is_fresh(self.cache, 100.0, lambda: 1050.0)
        )
        self.assertFalse(
            google_fonts.catalogue_is_fresh(self.cache, 100.0, lambda: 1150.0)
        )
        # A cache stamped in the future is a clock that cannot be reasoned
        # about. Refetching is the cheap, safe answer.
        self.assertFalse(
            google_fonts.catalogue_is_fresh(self.cache, 100.0, lambda: 900.0)
        )

    def test_a_cache_from_another_schema_version_is_not_trusted(self):
        self.cache.write_text(json.dumps({"schema_version": "something/v0"}),
                              encoding="utf-8")
        self.assertFalse(
            google_fonts.catalogue_is_fresh(self.cache, 10**9, lambda: 0.0)
        )
        self.cache.write_text("not json at all", encoding="utf-8")
        self.assertFalse(
            google_fonts.catalogue_is_fresh(self.cache, 10**9, lambda: 0.0)
        )


class DryRunTests(unittest.TestCase):
    def test_dry_run_opens_no_connection_and_names_every_host(self):
        def refuse(*_args, **_kwargs):
            raise AssertionError("--dry-run must not open a connection")

        original = google_fonts.urllib.request.urlopen
        google_fonts.urllib.request.urlopen = refuse
        try:
            with tempfile.TemporaryDirectory() as directory:
                self.assertEqual(
                    google_fonts.main(
                        ["--dry-run", "catalogue", str(Path(directory) / "c.json")]
                    ),
                    0,
                )
                self.assertEqual(
                    google_fonts.main(
                        ["--dry-run", "fetch", "Inter", str(Path(directory) / "out")]
                    ),
                    0,
                )
        finally:
            google_fonts.urllib.request.urlopen = original

    def test_the_described_hosts_are_the_hosts_that_are_allowed(self):
        described = google_fonts.dry_run_description("fetch", "Inter")
        self.assertFalse(described["sends_user_content"])
        self.assertEqual(
            set(described["allowed_hosts"]), set(google_fonts.ALLOWED_HOSTS)
        )


if __name__ == "__main__":
    unittest.main()
