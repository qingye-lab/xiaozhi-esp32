import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path

from PIL import Image, ImageChops


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "build_xiaoya_assets", ROOT / "scripts" / "build_xiaoya_assets.py"
)
BUILD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(BUILD)


class XiaoyaAssetTests(unittest.TestCase):
    def test_source_frames_are_small_real_alpha_pngs(self):
        source = ROOT / "main/boards/alientek/atk-dnesp32s3/artwork/xiaoya/frames"
        families = set(BUILD.EMOTION_FAMILIES.values()) | set(
            BUILD.STARTUP_ALIASES.values()
        )
        self.assertEqual({path.name for path in source.iterdir() if path.is_dir()}, families)
        for family in families:
            paths = sorted((source / family).glob("*.png"))
            self.assertEqual(len(paths), BUILD.SOURCE_FRAMES)
            for path in paths:
                with Image.open(path) as image:
                    self.assertEqual(image.size, (BUILD.SOURCE_SIZE, BUILD.SOURCE_SIZE))
                    self.assertIn("A", image.getbands())
                    alpha = image.convert("RGBA").getchannel("A")
                    self.assertEqual(alpha.getextrema()[0], 0)
                    self.assertIsNotNone(alpha.getbbox())
                    self.assertTrue(all(alpha.getpixel(point) <= 8 for point in (
                        (0, 0),
                        (BUILD.SOURCE_SIZE - 1, 0),
                        (0, BUILD.SOURCE_SIZE - 1),
                        (BUILD.SOURCE_SIZE - 1, BUILD.SOURCE_SIZE - 1),
                    )))
                    transparent_pixels = sum(value <= 8 for value in alpha.getdata())
                    self.assertGreater(transparent_pixels / (BUILD.SOURCE_SIZE ** 2), 0.20)

    def test_checked_in_collection_covers_emotion_contract_and_budget(self):
        output = ROOT / "main/boards/alientek/atk-dnesp32s3/assets/xiaoya"
        actual = {path.stem for path in output.glob("*.gif")}
        expected = set(BUILD.EMOTION_FAMILIES) | set(BUILD.STARTUP_ALIASES)
        self.assertEqual(actual, expected)
        self.assertEqual(len(BUILD.EMOTION_FAMILIES), 21)
        total = sum(path.stat().st_size for path in output.glob("*.gif"))
        self.assertLessEqual(total, BUILD.MAX_RUNTIME_BYTES)
        for path in output.glob("*.gif"):
            with Image.open(path) as image:
                self.assertEqual(image.size, (BUILD.CANVAS_SIZE, BUILD.CANVAS_SIZE))
                self.assertEqual(getattr(image, "n_frames", 1), BUILD.SOURCE_FRAMES)
                self.assertIn("transparency", image.info)

    def test_each_animation_family_contains_real_pose_changes(self):
        output = ROOT / "main/boards/alientek/atk-dnesp32s3/assets/xiaoya"
        representatives = {
            "angry": "angry",
            "happy": "happy",
            "idle": "neutral",
            "loving": "loving",
            "sad": "sad",
            "sleepy": "sleepy",
            "surprised": "surprised",
            "thinking": "thinking",
        }

        def normalize(frame):
            frame = frame.convert("RGBA")
            bbox = frame.getchannel("A").getbbox()
            self.assertIsNotNone(bbox)
            frame = frame.crop(bbox).resize((128, 128), Image.Resampling.LANCZOS)
            background = Image.new("RGBA", frame.size, (247, 250, 245, 255))
            return Image.alpha_composite(background, frame).convert("RGB")

        for family, emotion in representatives.items():
            with Image.open(output / f"{emotion}.gif") as image:
                image.seek(0)
                first = normalize(image.convert("RGBA"))
                image.seek(3)
                peak = normalize(image.convert("RGBA"))
            difference = ImageChops.difference(first, peak)
            changed = sum(max(pixel) > 24 for pixel in difference.getdata())
            self.assertGreater(
                changed / (128 * 128),
                0.08,
                f"{family} looks like a translated/scaled still rather than pose animation",
            )

    def test_builder_is_reproducible(self):
        source = ROOT / "main/boards/alientek/atk-dnesp32s3/artwork/xiaoya"
        with tempfile.TemporaryDirectory() as directory:
            total = BUILD.build_collection(source, Path(directory))
            self.assertLessEqual(total, BUILD.MAX_RUNTIME_BYTES)
            self.assertEqual(len(list(Path(directory).glob("*.gif"))), 22)

    def test_pure_kid_companion_logic(self):
        source = r'''
#include "main/boards/alientek/atk-dnesp32s3/kid_companion_logic.h"
#include "main/boards/common/camera.h"
#include <cassert>
#include <string>

class LegacyCamera final : public Camera {
public:
    void SetExplainUrl(const std::string&, const std::string&) override {}
    bool Capture() override { return true; }
    bool SetHMirror(bool) override { return true; }
    bool SetVFlip(bool) override { return true; }
    std::string Explain(const std::string&) override { return {}; }
};

int main() {
    using namespace kid_companion;

    assert(kDefaultOutputVolume == 45);
    assert(kMaximumOutputVolume == 70);
    assert(kCriticalI2cMaxAttempts == 3);
    assert(kCriticalI2cRetryDelayMs == 50);
    assert(ClampOutputVolume(-10) == 0);
    assert(ClampOutputVolume(45) == 45);
    assert(ClampOutputVolume(70) == 70);
    assert(ClampOutputVolume(100) == 70);
    assert(std::string(EnvironmentLightName(false, LightLevel::kNormal)) == "unknown");
    assert(std::string(EnvironmentLightName(true, LightLevel::kDark)) == "dark");

    LegacyCamera legacy_camera;
    std::string camera_reason = "stale";
    assert(legacy_camera.GetCaptureInstructions().empty());
    assert(legacy_camera.PrepareCapture(camera_reason));
    assert(camera_reason.empty());

    LightClassifier light;
    assert(light.Update(70) == LightLevel::kDark);
    assert(light.Update(120) == LightLevel::kDark);
    assert(light.Update(200) == LightLevel::kNormal);
    assert(light.Update(2600) == LightLevel::kBright);
    assert(light.Update(2000) == LightLevel::kBright);
    assert(light.Update(1700) == LightLevel::kNormal);

    ProximityClassifier proximity;
    assert(!proximity.Update(250));
    assert(proximity.Update(350));
    assert(proximity.Update(250));
    assert(!proximity.Update(150));

    MotionClassifier motion;
    assert(!motion.Update(0, 0, 1024));
    for (int i = 0; i < 10; ++i) {
        motion.Update(1, 1, 1024);
    }
    assert(motion.stable());
    assert(!motion.Update(300, 0, 1024));

    CameraConsentState consent;
    std::string reason;
    assert(!consent.Prepare(0, true, true, reason));
    assert(!consent.Arm(100, reason));
    consent.Request(100);
    assert(consent.IsRequestPending(101));
    assert(consent.Arm(101, reason));
    assert(!consent.IsRequestPending(101));
    assert(!consent.Prepare(101, true, false, reason));
    assert(consent.Prepare(101, true, true, reason));
    consent.Consume();
    assert(!consent.IsArmed(102));
    consent.Request(100);
    assert(consent.Arm(100, reason));
    assert(!consent.Prepare(20101, true, true, reason));
    consent.Request(100);
    assert(!consent.Arm(60101, reason));
    consent.Request(100);
    consent.Cancel();
    assert(!consent.IsRequestPending(101));
    assert(!consent.Arm(101, reason));

    ButtonDebouncer click;
    assert(click.Update(true, 0) == ButtonEvent::kNone);
    assert(click.Update(true, 31) == ButtonEvent::kNone);
    assert(click.Update(false, 100) == ButtonEvent::kNone);
    assert(click.Update(false, 131) == ButtonEvent::kClick);

    ButtonDebouncer long_press;
    long_press.Update(true, 0);
    long_press.Update(true, 31);
    assert(long_press.Update(true, 1531) == ButtonEvent::kLongPress);
    long_press.Update(false, 1600);
    assert(long_press.Update(false, 1631) == ButtonEvent::kNone);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            test_file = directory / "kid_logic_test.cc"
            binary = directory / "kid_logic_test"
            test_file.write_text(source, encoding="utf-8")
            subprocess.run(
                [
                    "c++",
                    "-std=c++17",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(test_file),
                    "-o",
                    str(binary),
                ],
                check=True,
            )
            subprocess.run([str(binary)], check=True)

    def test_offline_wifi_voice_starts_before_network(self):
        application = (ROOT / "main/application.cc").read_text(encoding="utf-8")
        initialize = application.split("void Application::Initialize()", 1)[1].split(
            "void Application::Run()", 1
        )[0]
        self.assertLess(
            initialize.index("LoadSpeechModels()"),
            initialize.index("audio_service_.Initialize(codec)"),
        )
        self.assertLess(
            initialize.index("audio_service_.EnableWakeWordDetection(true)"),
            initialize.index("board.StartNetwork()"),
        )
        self.assertIn('GetString("download_url").empty()', initialize)

        assets = (ROOT / "main/assets.cc").read_text(encoding="utf-8")
        loader = assets.split("bool Assets::LoadSrmodelsFromIndex", 1)[1].split(
            "#if HAVE_LVGL", 1
        )[0]
        self.assertIn("if (assets->models_list_ != nullptr)", loader)
        self.assertNotIn("esp_srmodel_deinit", loader)


if __name__ == "__main__":
    unittest.main()
