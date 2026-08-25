import importlib.util
import json
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_default_assets", ROOT / "scripts" / "build_default_assets.py"
)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class BuildDefaultAssetsTest(unittest.TestCase):
    def test_multinet_metadata_keeps_wake_and_offline_wifi_actions_separate(self):
        wake = {"wake_word": "ni hao xiao zhi", "display": "你好小智", "threshold": 0.25}
        wifi = {
            "command": "kai shi pei wang,chong xin pei wang",
            "display": "开始配网/重新配网",
        }
        info = BUILD.build_multinet_model_info(wake, wifi, ["mn5q8_cn"])
        self.assertEqual(info["language"], "cn")
        self.assertEqual(info["threshold"], 0.25)
        self.assertEqual(
            info["commands"],
            [
                {"command": "ni hao xiao zhi", "text": "你好小智", "action": "wake"},
                {
                    "command": "kai shi pei wang",
                    "text": "开始配网",
                    "action": "wifi_config",
                },
                {
                    "command": "chong xin pei wang",
                    "text": "重新配网",
                    "action": "wifi_config",
                },
            ],
        )

    def test_offline_wifi_command_requires_command_and_display(self):
        with tempfile.TemporaryDirectory() as directory:
            sdkconfig = Path(directory) / "sdkconfig"
            sdkconfig.write_text(
                'CONFIG_OFFLINE_WIFI_CONFIG_VOICE_COMMAND="kai shi pei wang"\n',
                encoding="utf-8",
            )
            self.assertIsNone(BUILD.read_offline_wifi_command_from_sdkconfig(sdkconfig))
            sdkconfig.write_text(
                'CONFIG_OFFLINE_WIFI_CONFIG_VOICE_COMMAND="kai shi pei wang"\n'
                'CONFIG_OFFLINE_WIFI_CONFIG_VOICE_COMMAND_DISPLAY="开始配网"\n',
                encoding="utf-8",
            )
            self.assertEqual(
                BUILD.read_offline_wifi_command_from_sdkconfig(sdkconfig),
                {"command": "kai shi pei wang", "display": "开始配网"},
            )

    def test_explicit_emoji_collection_path_takes_precedence(self):
        with tempfile.TemporaryDirectory() as explicit, tempfile.TemporaryDirectory() as noto:
            resolved = BUILD.resolve_emoji_collection_path(
                explicit, "missing-shared-collection", noto
            )
            self.assertEqual(resolved, str(Path(explicit).resolve()))

    def test_explicit_emoji_collection_path_must_exist(self):
        with tempfile.TemporaryDirectory() as directory:
            missing = Path(directory) / "missing"
            with self.assertRaisesRegex(ValueError, "Emoji collection directory not found"):
                BUILD.resolve_emoji_collection_path(str(missing), None, directory)

    def test_text_font_metadata_uses_bundle_charset_size_and_bpp(self):
        with tempfile.TemporaryDirectory() as directory:
            assets = Path(directory)
            BUILD.generate_index_json(
                str(assets),
                None,
                "font_noto_sans_common_20_4.bin",
                None,
                font_bundle_id="noto-v1",
            )
            index = json.loads((assets / "index.json").read_text(encoding="utf-8"))
            self.assertEqual(
                index["text_font_meta"],
                {"charset": "common", "size": 20, "bpp": 4, "bundle": "noto-v1"},
            )

    def test_text_font_requires_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                BUILD.generate_index_json(
                    directory, None, "font_noto_sans_common_20_4.bin", None
                )


if __name__ == "__main__":
    unittest.main()
