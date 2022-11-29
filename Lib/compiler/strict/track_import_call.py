# Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

from __future__ import annotations

from typing import final, Set


@final
class TrackImportCall:
    def __init__(self) -> None:
        self.tracked_modules: Set[str] = set()
        self.level: int = 0
        self.active: bool = True

    def enter_import(self) -> None:
        self.level += 1

    def exit_import(self) -> None:
        self.level -= 1

    def register(self, modname: str) -> None:
        if self.active and self.level > 0:
            self.tracked_modules.add(modname)

    def end(self) -> None:
        self.active = False

    def import_call_tracked(self, modname: str) -> bool:
        return modname in self.tracked_modules


tracker = TrackImportCall()
