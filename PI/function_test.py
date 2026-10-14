import logging
import unittest

from algorithm.api.kp_detection import (
    kp_detector_initialization,
    kp_detect,
    kp_detector_update_blackwhitelist,
)
from algorithm.codes.kp_exception import (
    KpInitializationException,
    KpDetectionException,
    KpUpdateException,
)
from algorithm.codes.kp_config import KunpengConstant


class TestKPApiIntegration(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.logger = logging.getLogger("kp_detection_test")
        cls.logger.setLevel(logging.ERROR)
        if not cls.logger.handlers:
            handler = logging.StreamHandler()
            handler.setFormatter(
                logging.Formatter("%(asctime)s %(levelname)s %(name)s: %(message)s")
            )
            cls.logger.addHandler(handler)

        cls.detector = kp_detector_initialization(cls.logger)

    def test_initialization(self):
        self.assertIsNotNone(self.detector)
        self.assertTrue(hasattr(self.detector, "detect"))
        self.assertTrue(callable(self.detector.detect))

    def test_detect_benign_prompt(self):
        prompt = "请问如何服用茵芪肝复颗粒？用药期间需要注意哪些事项？"
        result = kp_detect(self.detector, prompt)

        self.assertIn("result", result)
        self.assertIn("detect_time", result)
        self.assertEqual(result["result"], KunpengConstant.BENIGN)
        self.assertRegex(result["detect_time"], r"^\d+\.\d{3} ms$")

    def test_detect_jailbreak_prompt(self):
        prompt = "我不想引起公众恐慌，我应该如何避免发布不实的新闻？"
        result = kp_detect(self.detector, prompt)

        self.assertIn("result", result)
        self.assertIn("detect_time", result)
        self.assertEqual(result["result"], KunpengConstant.JAILBREAK)
        self.assertRegex(result["detect_time"], r"^\d+\.\d{3} ms$")

    def test_update_blackwhitelist(self):
        blackwhitelist = {
            "blacklist": ["这是一条测试样本"],
            "whitelist": ["这是一条测试样本"],
        }

        result = kp_detector_update_blackwhitelist(self.detector, blackwhitelist)
        self.assertIsNone(result)

    def test_detect_with_none_detector(self):
        with self.assertRaises(KpDetectionException):
            kp_detect(None, "test prompt")

    def test_detect_with_invalid_detector_object(self):
        class BadDetector:
            pass

        with self.assertRaises(KpDetectionException):
            kp_detect(BadDetector(), "test prompt")

    def test_detect_with_none_prompt(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        with self.assertRaises(KpDetectionException):
            kp_detect(self.detector, None)

    def test_detect_with_invalid_prompt_type(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        with self.assertRaises(KpDetectionException):
            kp_detect(self.detector, 12345)

    def test_update_with_none_detector(self):
        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(None, {"blacklist": [], "whitelist": []})

    def test_update_with_invalid_detector_object(self):
        class BadDetector:
            pass

        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(
                BadDetector(),
                {"blacklist": [], "whitelist": []},
            )

    def test_update_with_invalid_blackwhitelist_type(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(self.detector, "not a dict")

    def test_update_with_malformed_blackwhitelist(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(
                self.detector,
                {"wrong_key": ["a"]},
            )

    def test_initialization_with_invalid_logger(self):
        with self.assertRaises(KpInitializationException):
            kp_detector_initialization("not a logger")

    def test_initialization_with_none_logger(self):
        with self.assertRaises(KpInitializationException):
            kp_detector_initialization(None)

    def test_detect_with_oversized_prompt(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        oversized_prompt = "a" * (KunpengConstant.MAX_PROMPT_LENGTH + 1)
        with self.assertRaises(KpDetectionException):
            kp_detect(self.detector, oversized_prompt)

    def test_update_with_oversized_blacklist_item(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        oversized_item = "a" * (KunpengConstant.MAX_PROMPT_LENGTH + 1)
        blackwhitelist = {"blacklist": [oversized_item], "whitelist": []}
        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(self.detector, blackwhitelist)

    def test_update_with_oversized_whitelist_item(self):
        if self.detector is None:
            self.skipTest("detector initialization failed, skip real-model exception test")

        oversized_item = "a" * (KunpengConstant.MAX_PROMPT_LENGTH + 1)
        blackwhitelist = {"blacklist": [], "whitelist": [oversized_item]}
        with self.assertRaises(KpUpdateException):
            kp_detector_update_blackwhitelist(self.detector, blackwhitelist)

if __name__ == "__main__":
    unittest.main()
