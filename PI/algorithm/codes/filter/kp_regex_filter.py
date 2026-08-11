from logging import Logger

from algorithm.codes.utils.kp_utils import time_stat


class RegexFilter:
    def __init__(self, config, logger: Logger):
        self.logger = logger

        self.naive_regex_patterns = config.naive_regex

    def naive_regex_match(self, prompt):
        for pattern in self.naive_regex_patterns:
            if pattern.search(prompt):
                return True
        return False

    @time_stat
    def run_filter(self, prompt):
        try:
            return self.naive_regex_match(prompt)
        except Exception:
            raise
        