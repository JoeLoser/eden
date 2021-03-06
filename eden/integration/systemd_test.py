#!/usr/bin/env python3
#
# Copyright (c) 2016-present, Facebook, Inc.
# All rights reserved.
#
# This source code is licensed under the BSD-style license found in the
# LICENSE file in the root directory of this source tree. An additional grant
# of patent rights can be found in the PATENTS file in the same directory.

import pathlib
import subprocess
import sys
import typing
import unittest

import pexpect
from eden.test_support.environment_variable import EnvironmentVariableMixin
from eden.test_support.temporary_directory import TemporaryDirectoryMixin

from .lib.edenfs_systemd import EdenFSSystemdMixin
from .lib.find_executables import FindExe
from .lib.systemd import SystemdUserServiceManagerMixin


class SystemdTest(
    unittest.TestCase,
    EnvironmentVariableMixin,
    TemporaryDirectoryMixin,
    SystemdUserServiceManagerMixin,
    EdenFSSystemdMixin,
):
    """Test Eden's systemd service for Linux."""

    def setUp(self) -> None:
        super().setUp()

        self.set_environment_variable("EDEN_EXPERIMENTAL_SYSTEMD", "1")
        self.eden_dir = self.make_temporary_directory()
        self.etc_eden_dir = self.make_temporary_directory()
        self.home_dir = self.make_temporary_directory()

    # TODO(T33122320): Delete this test when systemd is properly integrated.
    # TODO(T33122320): Test without --foreground.
    def test_eden_start_says_systemd_mode_is_enabled(self) -> None:
        def test(start_args: typing.List[str]) -> None:
            with self.subTest(start_args=start_args):
                start_process: "pexpect.spawn[str]" = pexpect.spawn(
                    FindExe.EDEN_CLI,
                    self.get_required_eden_cli_args()
                    + ["start", "--foreground"]
                    + start_args,
                    encoding="utf-8",
                    logfile=sys.stderr,
                )
                start_process.expect_exact("Running in experimental systemd mode")
                start_process.expect_exact("Started edenfs")

        test(start_args=["--", "--allowRoot"])
        test(
            start_args=[
                "--daemon-binary",
                typing.cast(str, FindExe.FAKE_EDENFS),  # T38947910
            ]
        )

    # TODO(T33122320): Delete this test when systemd is properly integrated.
    def test_eden_start_with_systemd_disabled_does_not_say_systemd_mode_is_enabled(
        self
    ) -> None:
        self.unset_environment_variable("EDEN_EXPERIMENTAL_SYSTEMD")

        def test(start_args: typing.List[str]) -> None:
            with self.subTest(start_args=start_args):
                start_process: "pexpect.spawn[str]" = pexpect.spawn(
                    FindExe.EDEN_CLI,
                    self.get_required_eden_cli_args()
                    + ["start", "--foreground"]
                    + start_args,
                    encoding="utf-8",
                    logfile=sys.stderr,
                )
                start_process.expect_exact("Started edenfs")
                self.assertNotIn(
                    "Running in experimental systemd mode", start_process.before
                )

        test(start_args=["--", "--allowRoot"])
        test(
            start_args=[
                "--daemon-binary",
                typing.cast(str, FindExe.FAKE_EDENFS),  # T38947910
            ]
        )

    def test_eden_start_starts_systemd_service(self) -> None:
        self.set_up_edenfs_systemd_service()
        subprocess.check_call(
            [typing.cast(str, FindExe.EDEN_CLI)]  # T38947910
            + self.get_required_eden_cli_args()
            + [
                "start",
                "--daemon-binary",
                typing.cast(str, FindExe.FAKE_EDENFS),  # T38947910
            ]
        )
        self.assert_systemd_service_is_active(eden_dir=pathlib.Path(self.eden_dir))

    def test_systemd_service_is_failed_if_edenfs_crashes_on_start(self) -> None:
        self.set_up_edenfs_systemd_service()
        self.assert_systemd_service_is_stopped(eden_dir=pathlib.Path(self.eden_dir))
        subprocess.call(
            [typing.cast(str, FindExe.EDEN_CLI)]  # T38947910
            + self.get_required_eden_cli_args()
            + [
                "start",
                "--daemon-binary",
                typing.cast(str, FindExe.FAKE_EDENFS),  # T38947910
                "--",
                "--failDuringStartup",
            ]
        )
        self.assert_systemd_service_is_failed(eden_dir=pathlib.Path(self.eden_dir))

    def test_eden_start_reports_service_failure_if_edenfs_fails_during_startup(
        self
    ) -> None:
        self.set_up_edenfs_systemd_service()
        start_process = self.spawn_start_with_fake_edenfs(
            extra_args=["--", "--failDuringStartup"]
        )
        start_process.expect(
            r"Job for fb-edenfs@.+?\.service failed because "
            r"the control process exited with error code"
        )
        # TODO(strager): Remove this message. journalctl is unreliable and
        # unhelpful for users.
        start_process.expect_exact("journalctl")

    def test_eden_start_reports_error_if_systemd_is_not_running(self) -> None:
        self.set_up_edenfs_systemd_service()
        systemd = self.systemd
        assert systemd is not None
        systemd.exit()

        start_process = self.spawn_start_with_fake_edenfs()
        # TODO(strager): Improve this message.
        start_process.expect_exact("Failed to connect to bus: Connection refused")

    def test_eden_start_reports_error_if_systemd_environment_is_missing(self) -> None:
        self.set_up_edenfs_systemd_service()
        self.unset_environment_variable("XDG_RUNTIME_DIR")

        start_process = self.spawn_start_with_fake_edenfs()
        start_process.expect_exact(
            "error: The XDG_RUNTIME_DIR environment variable is not set"
        )

    def spawn_start_with_fake_edenfs(
        self, extra_args: typing.Sequence[str] = ()
    ) -> "pexpect.spawn[str]":
        return pexpect.spawn(
            FindExe.EDEN_CLI,
            self.get_required_eden_cli_args()
            + [
                "start",
                "--daemon-binary",
                typing.cast(str, FindExe.FAKE_EDENFS),  # T38947910
            ]
            + list(extra_args),
            encoding="utf-8",
            logfile=sys.stderr,
        )

    def get_required_eden_cli_args(self) -> typing.List[str]:
        return [
            "--config-dir",
            self.eden_dir,
            "--etc-eden-dir",
            self.etc_eden_dir,
            "--home-dir",
            self.home_dir,
        ]
