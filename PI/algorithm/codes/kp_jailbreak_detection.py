from logging import Logger

from algorithm.codes.kp_config import *
from algorithm.codes.kp_exception import *
from algorithm.codes.filter.kp_regex_filter import RegexFilter
from algorithm.codes.filter.kp_classifier_filter import ClassifierFilter
from algorithm.codes.filter.kp_blackwhitelist_filter import BlackwhitelistFilter


class JailbreakDetection:
    def __init__(self, logger: Logger):
        self._verify_logger(logger)
        self.logger = logger

        try:
            self.RegexFilter = self.init_regex_filter()
            self.ClassifierFilter = self.init_classifier_filter()
            self.BlackwhitelistFilter = self.init_blackwhitelist_filter()
        except Exception as e:
            raise KpInitializationException(
                KpErrorCode.InitError,
                str(e)
            ) from e

    @staticmethod
    def _verify_logger(logger):
        if not isinstance(logger, Logger):
            raise KpInitializationException(
                KpErrorCode.InitError,
                "invalid logger type."
            )

    @staticmethod
    def _verify_blackwhitelist(blackwhitelist):
        if not isinstance(blackwhitelist, dict):
            raise KpUpdateException(
                KpErrorCode.UpdateError,
                "blackwhitelist is not dict formatted."
            )

        for key, value in blackwhitelist.items():
            if key not in KunpengConstant.ALLOWED_CONFIG_KEYS:
                raise KpUpdateException(
                    KpErrorCode.UpdateError,
                    "key must be blacklist or whitelist."
                )

            if not isinstance(value, KunpengConstant.ALLOWED_CONFIG_TYPES[key]):
                raise KpUpdateException(
                    KpErrorCode.UpdateError,
                    "list is not list formatted."
                )

            if len(value) > KunpengConstant.ALLOWED_CONFIG_SIZE[key]:
                raise KpUpdateException(
                    KpErrorCode.UpdateError,
                    f"{key} exceed the length limit."
                )

            for item in value:
                if not isinstance(item, str):
                    raise KpUpdateException(
                        KpErrorCode.UpdateError,
                        "every item must be str formatted."
                    )

                if len(item) > KunpengConstant.MAX_PROMPT_LENGTH:
                    raise KpUpdateException(
                        KpErrorCode.UpdateError,
                        f"every item's length must be less than {KunpengConstant.MAX_PROMPT_LENGTH}."
                    )

    @staticmethod
    def _verify_prompt(prompt):
        if not isinstance(prompt, str):
            raise KpDetectionException(
                KpErrorCode.DetectError,
                "prompt must be str formatted."
            )

        if len(prompt) > KunpengConstant.MAX_PROMPT_LENGTH:
            raise KpDetectionException(
                KpErrorCode.DetectError,
                f"prompt exceeds max length: {len(prompt)}/{KunpengConstant.MAX_PROMPT_LENGTH}."
            )

    def init_regex_filter(self):
        regex_config = RegexConfig()
        return RegexFilter(regex_config, self.logger)

    def init_classifier_filter(self):
        classifier_config = ClassifierConfig()
        return ClassifierFilter(classifier_config, self.logger)

    def init_blackwhitelist_filter(self):
        blackwhitelist_config = BlackwhitelistConfig()
        return BlackwhitelistFilter(blackwhitelist_config, self.logger)

    def update_blackwhitelist(self, blackwhitelist):
        self._verify_blackwhitelist(blackwhitelist)
        try:
            self.BlackwhitelistFilter.add_blackwhitelist(blackwhitelist)
        except Exception as e:
            raise KpUpdateException(
                KpErrorCode.UpdateError,
                str(e)
            ) from e

    def detect(self, prompt):
        self._verify_prompt(prompt)

        try:
            # step 1 : Blackwhitelist Filter
            list_name, duration = self.BlackwhitelistFilter.run_filter(prompt)
            self.logger.debug(f"{KunpengConstant.LOG_PREFIX}Bwlst Stage spend: {duration * 1000:.3f} ms")
            if list_name:
                return True if list_name == "blacklist" else False

            # step 2 : Regex Filter
            regex_result, duration = self.RegexFilter.run_filter(prompt)
            self.logger.debug(f"{KunpengConstant.LOG_PREFIX}Regex Stage spend: {duration * 1000:.3f} ms")
            if regex_result:
                return True

            # step 3 : Classifier Filter
            clf_result, duration = self.ClassifierFilter.run_filter(prompt)
            self.logger.info(f"{KunpengConstant.LOG_PREFIX}Model Stage spend: {duration * 1000:.3f} ms")

            return clf_result
        except Exception as e:
            raise KpDetectionException(
                KpErrorCode.DetectError,
                str(e)
            ) from e
