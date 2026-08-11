import base64
from typing import List, Dict, Literal, Any
from logging import Logger

import numpy as np
import jieba
from datasketch import MinHash, MinHashLSH

from algorithm.codes.utils.kp_utils import time_stat, load_json
from algorithm.codes.kp_config import KunpengConstant


class BlackwhitelistFilter:
    LIST_NAMES = ("whitelist", "blacklist")

    def __init__(self, config: Dict, logger: Logger):
        self.logger = logger

        self.seed = config.seed
        self.schema = config.schema
        self.num_perm = config.num_perm
        self.threshold = config.threshold
        self.blackwhitelist_path = config.blackwhitelist_path
        self.jieba_tmp_dir = config.jieba_tmp_dir
        self.jieba_init_sentence = config.jieba_init_sentence

        self.tokenizer = self._init_tokenizer()
        self.idx = 0
        self.extra_list_num = {list_name: 0 for list_name in self.LIST_NAMES}
        self.blackwhitelist = self._load_blackwhitelist(self.blackwhitelist_path)

    @staticmethod
    def minhash_to_base64(m: MinHash) -> str:
        arr = np.asarray(m.hashvalues, dtype="<u8")
        return base64.b64encode(arr.tobytes()).decode("ascii")

    @staticmethod
    def base64_to_minhash(sig: str, num_perm: int = 128, seed: int = 42, schema: str = "affine32"):
        raw = base64.b64decode(sig.encode("ascii"))
        hash_values = np.frombuffer(raw, dtype="<u8")

        if len(hash_values) != num_perm:
            raise ValueError(f"Signature Decode Failed: num perm mismatched.")

        return MinHash(num_perm=num_perm, seed=seed, hashvalues=hash_values, scheme=schema)

    def _init_tokenizer(self):
        jieba.setLogLevel(20)
        tokenizer = jieba.Tokenizer()
        tokenizer.tmp_dir = self.jieba_tmp_dir
        tokenizer.lcut(self.jieba_init_sentence, cut_all=False)
        return tokenizer

    def _init_lsh_filter(self):
        return MinHashLSH(self.threshold, self.num_perm)

    def _empty_blackwhitelist(self) -> Dict[Literal["whitelist", "blacklist"], MinHashLSH]:
        return {
            "blacklist": self._init_lsh_filter(),
            "whitelist": self._init_lsh_filter(),
        }

    def _load_one_list(
        self,
        lsh: MinHashLSH,
        signatures: List[str],
    ) -> None:
        for sig in signatures:
            m = self.base64_to_minhash(
                sig=sig,
                num_perm=self.num_perm,
                seed=self.seed,
                schema=self.schema
            )
            lsh.insert(self.idx, m, check_duplication=True)
            self.idx += 1

    def _load_blackwhitelist(self, blackwhitelist_path: str):
        blackwhitelist = self._empty_blackwhitelist()

        try:
            json_obj = load_json(blackwhitelist_path)

            for list_name in self.LIST_NAMES:
                self._load_one_list(
                    lsh=blackwhitelist[list_name],
                    signatures=json_obj.get(list_name, [])
                )

        except (FileNotFoundError, PermissionError, ValueError):
            raise

        return blackwhitelist

    def _build_minhash(self, text: str) -> MinHash:
        m = MinHash(self.num_perm, self.seed, scheme=self.schema)

        for token in self._lcut(text):
            m.update(token.encode("utf-8"))
        return m

    def _lcut(self, text: str) -> List[str]:
        return self.tokenizer.lcut(text)

    def add_blackwhitelist(self, blackwhitelist: Dict[Literal["whitelist", "blacklist"], List[str]]):
        succ = {list_name: 0 for list_name in blackwhitelist.keys()}

        for list_name in blackwhitelist.keys():
            if self.extra_list_num[list_name] == KunpengConstant.ALLOWED_CONFIG_SIZE[list_name]:
                self.logger.warning(f"{KunpengConstant.LOG_PREFIX}The maximum capacity "
                                    f"{KunpengConstant.ALLOWED_CONFIG_SIZE[list_name]} of {list_name} has been reached.")
                continue
            for text in blackwhitelist[list_name]:
                self.blackwhitelist[list_name].insert(self.idx, self._build_minhash(text))
                self.idx += 1
                self.extra_list_num[list_name] += 1
                succ[list_name] += 1

        for k, v in succ.items():
            if v:
                self.logger.info(f"{KunpengConstant.LOG_PREFIX}Blackwhitelist add success: {k} add {v}.")

    def _save_to_blackwhitelist(
        self,
        blackwhitelist: Dict[Literal["whitelist", "blacklist"], List[str]],
    ):
        try:
            json_obj = load_json(self.blackwhitelist_path)
        except (FileNotFoundError, PermissionError, ValueError):
            json_obj = {}

        for list_name in self.LIST_NAMES:
            json_obj.setdefault(list_name, [])

            for text in blackwhitelist.get(list_name, []):
                m = self._build_minhash(text)
                sig = self.minhash_to_base64(m)

                json_obj[list_name].append(sig)

                self.blackwhitelist[list_name].insert(self.idx, m)
                self.idx += 1

        import json
        with open(self.blackwhitelist_path, "w", encoding="utf-8") as f:
            json.dump(json_obj, f, ensure_ascii=False, indent=2)

        self.logger.info(f"{KunpengConstant.LOG_PREFIX}Blackwhitelist save success.")

    @time_stat
    def run_filter(self, prompt: str):
        m = self._build_minhash(prompt)

        for list_name in self.LIST_NAMES:
            if self.blackwhitelist[list_name].query(m):
                return list_name

        return None
