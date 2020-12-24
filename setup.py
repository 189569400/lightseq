import os
import re
import sys
import platform
import subprocess

from setuptools import setup, Extension
import setuptools
from setuptools.command.build_ext import build_ext
from distutils.version import LooseVersion

ENABLE_FP32 = int(os.environ.get("ENABLE_FP32", 0))
ENABLE_DEBUG = int(os.environ.get("ENABLE_DEBUG", 0))


class CMakeExtension(Extension):
    def __init__(self, name, sourcedir="", *args, **kwargs):
        Extension.__init__(self, name, sources=[], *args, **kwargs)
        self.sourcedir = os.path.abspath(sourcedir)


class CMakeBuild(build_ext):
    def run(self):
        try:
            out = subprocess.check_output(["cmake", "--version"])
        except OSError:
            raise RuntimeError(
                "CMake must be installed to build the following extensions: "
                + ", ".join(e.name for e in self.extensions)
            )

        if platform.system() == "Windows":
            cmake_version = LooseVersion(
                re.search(r"version\s*([\d.]+)", out.decode()).group(1)
            )
            if cmake_version < "3.1.0":
                raise RuntimeError("CMake >= 3.1.0 is required on Windows")

        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        # required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cmake_args = [
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=" + extdir,
            "-DPYTHON_EXECUTABLE=" + sys.executable,
        ]

        cfg = "Release"
        build_args = ["--config", cfg]

        if platform.system() == "Windows":
            cmake_args += [
                "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}".format(cfg.upper(), extdir)
            ]
            if sys.maxsize > 2 ** 32:
                cmake_args += ["-A", "x64"]
            build_args += ["--", "/m"]
        else:
            cmake_args += ["-DCMAKE_BUILD_TYPE=" + cfg]
            if not ENABLE_FP32:
                cmake_args += ["-DFP16_MODE=ON"]
            if ENABLE_DEBUG:
                cmake_args += ["-DDEBUG_MODE=ON"]
            build_args += ["--target", "lightseq"]
            build_args += ["--", "-j"]

        env = os.environ.copy()
        env["CXXFLAGS"] = '{} -DVERSION_INFO=\\"{}\\"'.format(
            env.get("CXXFLAGS", ""), self.distribution.get_version()
        )
        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)
        subprocess.check_call(
            ["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp, env=env
        )
        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=self.build_temp
        )


setup(
    name="lightseq",
    version="0.1.0",
    author="Ying Xiong",
    author_email="xiongying.taka@bytedance.com",
    description="python wrapper of LightSeq, LightSeq is a high performance inference library for SOTA NLU/NLG models",
    long_description="",
    ext_modules=[CMakeExtension("lightseq")],
    cmdclass=dict(build_ext=CMakeBuild),
    zip_safe=False,
    packages=setuptools.find_packages(),
)
