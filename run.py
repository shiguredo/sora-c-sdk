import argparse
import logging
import multiprocessing
import os
import platform
import shutil
import stat
import subprocess
import sys
import tarfile
import urllib.parse
import zipfile
from typing import Dict, Optional

logging.basicConfig(level=logging.DEBUG)


class ChangeDirectory(object):
    def __init__(self, cwd):
        self._cwd = cwd

    def __enter__(self):
        self._old_cwd = os.getcwd()
        logging.debug(f"pushd {self._old_cwd} --> {self._cwd}")
        os.chdir(self._cwd)

    def __exit__(self, exctype, excvalue, trace):
        logging.debug(f"popd {self._old_cwd} <-- {self._cwd}")
        os.chdir(self._old_cwd)
        return False


def cd(cwd):
    return ChangeDirectory(cwd)


def cmd(args, **kwargs):
    logging.debug(f"+{args} {kwargs}")
    if "check" not in kwargs:
        kwargs["check"] = True
    if "resolve" in kwargs:
        resolve = kwargs["resolve"]
        del kwargs["resolve"]
    else:
        resolve = True
    if resolve:
        args = [shutil.which(args[0]), *args[1:]]
    return subprocess.run(args, **kwargs)


# 標準出力をキャプチャするコマンド実行。シェルの `cmd ...` や $(cmd ...) と同じ
def cmdcap(args, **kwargs):
    # 3.7 でしか使えない
    # kwargs['capture_output'] = True
    kwargs["stdout"] = subprocess.PIPE
    kwargs["stderr"] = subprocess.PIPE
    kwargs["encoding"] = "utf-8"
    return cmd(args, **kwargs).stdout.strip()


# https://stackoverflow.com/a/2656405
def onerror(func, path, exc_info):
    """
    Error handler for ``shutil.rmtree``.
    If the error is due to an access error (read only file)
    it attempts to add write permission and then retries.
    If the error is for another reason it re-raises the error.

    Usage : ``shutil.rmtree(path, onerror=onerror)``
    """
    import stat

    # Is the error an access error?
    if not os.access(path, os.W_OK):
        os.chmod(path, stat.S_IWUSR)
        func(path)
    else:
        raise


def rm_rf(path: str):
    if not os.path.exists(path):
        logging.debug(f"rm -rf {path} => path not found")
        return
    if os.path.isfile(path) or os.path.islink(path):
        os.remove(path)
        logging.debug(f"rm -rf {path} => file removed")
    if os.path.isdir(path):
        shutil.rmtree(path, onerror=onerror)
        logging.debug(f"rm -rf {path} => directory removed")


def mkdir_p(path: str):
    if os.path.exists(path):
        logging.debug(f"mkdir -p {path} => already exists")
        return
    os.makedirs(path, exist_ok=True)
    logging.debug(f"mkdir -p {path} => directory created")


if platform.system() == "Windows":
    PATH_SEPARATOR = ";"
else:
    PATH_SEPARATOR = ":"


def add_path(path: str, is_after=False):
    logging.debug(f"add_path: {path}")
    if "PATH" not in os.environ:
        os.environ["PATH"] = path
        return

    if is_after:
        os.environ["PATH"] = os.environ["PATH"] + PATH_SEPARATOR + path
    else:
        os.environ["PATH"] = path + PATH_SEPARATOR + os.environ["PATH"]


def download(
    url: str, output_dir: Optional[str] = None, filename: Optional[str] = None
) -> str:
    if filename is None:
        output_path = urllib.parse.urlparse(url).path.split("/")[-1]
    else:
        output_path = filename

    if output_dir is not None:
        output_path = os.path.join(output_dir, output_path)

    if os.path.exists(output_path):
        return output_path

    try:
        if shutil.which("curl") is not None:
            cmd(["curl", "-fLo", output_path, url])
        else:
            cmd(["wget", "-cO", output_path, url])
    except Exception:
        # ゴミを残さないようにする
        if os.path.exists(output_path):
            os.remove(output_path)
        raise

    return output_path


def read_version_file(path: str) -> Dict[str, str]:
    versions = {}

    lines = open(path).readlines()
    for line in lines:
        line = line.strip()

        # コメント行
        if line[:1] == "#":
            continue

        # 空行
        if len(line) == 0:
            continue

        [a, b] = map(lambda x: x.strip(), line.split("=", 2))
        versions[a] = b.strip('"')

    return versions


# dir 以下にある全てのファイルパスを、dir2 からの相対パスで返す
def enum_all_files(dir, dir2):
    for root, _, files in os.walk(dir):
        for file in files:
            yield os.path.relpath(os.path.join(root, file), dir2)


def versioned(func):
    def wrapper(version, version_file, *args, **kwargs):
        if "ignore_version" in kwargs:
            if kwargs.get("ignore_version"):
                rm_rf(version_file)
            del kwargs["ignore_version"]

        if os.path.exists(version_file):
            ver = open(version_file).read()
            if ver.strip() == version.strip():
                return

        r = func(version=version, *args, **kwargs)

        with open(version_file, "w") as f:
            f.write(version)

        return r

    return wrapper


# アーカイブが単一のディレクトリに全て格納されているかどうかを調べる。
#
# 単一のディレクトリに格納されている場合はそのディレクトリ名を返す。
# そうでない場合は None を返す。
def _is_single_dir(infos, get_name, is_dir) -> Optional[str]:
    # tarfile: ['path', 'path/to', 'path/to/file.txt']
    # zipfile: ['path/', 'path/to/', 'path/to/file.txt']
    # どちらも / 区切りだが、ディレクトリの場合、後ろに / が付くかどうかが違う
    dirname = None
    for info in infos:
        name = get_name(info)
        n = name.rstrip("/").find("/")
        if n == -1:
            # ルートディレクトリにファイルが存在している
            if not is_dir(info):
                return None
            dir = name.rstrip("/")
        else:
            dir = name[0:n]
        # ルートディレクトリに２個以上のディレクトリが存在している
        if dirname is not None and dirname != dir:
            return None
        dirname = dir

    return dirname


def is_single_dir_tar(tar: tarfile.TarFile) -> Optional[str]:
    return _is_single_dir(tar.getmembers(), lambda t: t.name, lambda t: t.isdir())


def is_single_dir_zip(zip: zipfile.ZipFile) -> Optional[str]:
    return _is_single_dir(zip.infolist(), lambda z: z.filename, lambda z: z.is_dir())


# 解凍した上でファイル属性を付与する
def _extractzip(z: zipfile.ZipFile, path: str):
    z.extractall(path)
    if platform.system() == "Windows":
        return
    for info in z.infolist():
        if info.is_dir():
            continue
        filepath = os.path.join(path, info.filename)
        mod = info.external_attr >> 16
        if (mod & 0o120000) == 0o120000:
            # シンボリックリンク
            with open(filepath, "r") as f:
                src = f.read()
            os.remove(filepath)
            with cd(os.path.dirname(filepath)):
                if os.path.exists(src):
                    os.symlink(src, filepath)
        if os.path.exists(filepath):
            # 普通のファイル
            os.chmod(filepath, mod & 0o777)


# zip または tar.gz ファイルを展開する。
#
# 展開先のディレクトリは {output_dir}/{output_dirname} となり、
# 展開先のディレクトリが既に存在していた場合は削除される。
#
# もしアーカイブの内容が単一のディレクトリであった場合、
# そのディレクトリは無いものとして展開される。
#
# つまりアーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/file1
# - out/libsora/file2
# が出力される。
#
# また、アーカイブ libsora-1.23.tar.gz の内容が
# ['libsora-1.23', 'libsora-1.23/file1', 'libsora-1.23/file2', 'LICENSE']
# であった場合、extract('libsora-1.23.tar.gz', 'out', 'libsora') のようにすると
# - out/libsora/libsora-1.23/file1
# - out/libsora/libsora-1.23/file2
# - out/libsora/LICENSE
# が出力される。
def extract(
    file: str, output_dir: str, output_dirname: str, filetype: Optional[str] = None
):
    path = os.path.join(output_dir, output_dirname)
    logging.info(f"Extract {file} to {path}")
    if filetype == "gzip" or file.endswith(".tar.gz"):
        rm_rf(path)
        with tarfile.open(file) as t:
            dir = is_single_dir_tar(t)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                t.extractall(path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                t.extractall(output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    elif filetype == "zip" or file.endswith(".zip"):
        rm_rf(path)
        with zipfile.ZipFile(file) as z:
            dir = is_single_dir_zip(z)
            if dir is None:
                os.makedirs(path, exist_ok=True)
                # z.extractall(path)
                _extractzip(z, path)
            else:
                logging.info(f"Directory {dir} is stripped")
                path2 = os.path.join(output_dir, dir)
                rm_rf(path2)
                # z.extractall(output_dir)
                _extractzip(z, output_dir)
                if path != path2:
                    logging.debug(f"mv {path2} {path}")
                    os.replace(path2, path)
    else:
        raise Exception("file should end with .tar.gz or .zip")


def clone_and_checkout(url, version, dir, fetch, fetch_force):
    if fetch_force:
        rm_rf(dir)

    if not os.path.exists(os.path.join(dir, ".git")):
        cmd(["git", "clone", url, dir])
        fetch = True

    if fetch:
        with cd(dir):
            cmd(["git", "fetch"])
            cmd(["git", "reset", "--hard"])
            cmd(["git", "clean", "-df"])
            cmd(["git", "checkout", "-f", version])


def git_clone_shallow(url, hash, dir):
    rm_rf(dir)
    mkdir_p(dir)
    with cd(dir):
        cmd(["git", "init"])
        cmd(["git", "remote", "add", "origin", url])
        cmd(["git", "fetch", "--depth=1", "origin", hash])
        cmd(["git", "reset", "--hard", "FETCH_HEAD"])


def apply_patch(patch, dir, depth):
    patch = os.path.abspath(patch)
    with cd(dir):
        logging.info(f"patch -p{depth} < {patch}")
        if platform.system() == "Windows":
            cmd(
                [
                    "git",
                    "apply",
                    f"-p{depth}",
                    "--ignore-space-change",
                    "--ignore-whitespace",
                    "--whitespace=nowarn",
                    patch,
                ]
            )
        else:
            with open(patch) as stdin:
                cmd(["patch", f"-p{depth}"], stdin=stdin)


def cmake_path(path: str) -> str:
    return path.replace("\\", "/")


@versioned
def install_cmake(version, source_dir, install_dir, platform: str, ext):
    url = f"https://github.com/Kitware/CMake/releases/download/v{version}/cmake-{version}-{platform}.{ext}"
    path = download(url, source_dir)
    extract(path, install_dir, "cmake")
    # Android で自前の CMake を利用する場合、ninja へのパスが見つけられない問題があるので、同じディレクトリに symlink を貼る
    # https://issuetracker.google.com/issues/206099937
    if platform.startswith("linux"):
        with cd(os.path.join(install_dir, "cmake", "bin")):
            cmd(["ln", "-s", "/usr/bin/ninja", "ninja"])


BASE_DIR = os.path.abspath(os.path.dirname(__file__))


@versioned
def install_openh264(version, source_dir, install_dir):
    rm_rf(os.path.join(source_dir, "openh264"))
    rm_rf(os.path.join(install_dir, "openh264"))
    git_clone_shallow(
        "https://github.com/cisco/openh264.git",
        version,
        os.path.join(source_dir, "openh264"),
    )
    with cd(os.path.join(source_dir, "openh264")):
        cmd(
            [
                "make",
                f'PREFIX={os.path.join(install_dir, "openh264")}',
                "install-headers",
            ]
        )


@versioned
def install_aom(version, source_dir, build_dir, install_dir, cmake_args):
    rm_rf(os.path.join(source_dir, "aom"))
    rm_rf(os.path.join(build_dir, "aom"))
    rm_rf(os.path.join(install_dir, "aom"))
    git_clone_shallow(
        "https://aomedia.googlesource.com/aom",
        version,
        os.path.join(source_dir, "aom"),
    )
    with cd(os.path.join(source_dir, "aom")):
        cmd(
            [
                "cmake",
                "-B",
                os.path.join(build_dir, "aom"),
                f'-DCMAKE_INSTALL_PREFIX={os.path.join(install_dir, "aom")}',
                "-DBUILD_SHARED_LIBS=ON",
                *cmake_args,
            ]
        )
        cmd(
            [
                "cmake",
                "--build",
                os.path.join(build_dir, "aom"),
                f"-j{multiprocessing.cpu_count()}",
                "--config",
                "Release",
            ]
        )
        cmd(["cmake", "--install", os.path.join(build_dir, "aom")])


@versioned
def install_mbedtls(version, source_dir, build_dir, install_dir, cmake_args):
    rm_rf(os.path.join(source_dir, "mbedtls"))
    rm_rf(os.path.join(build_dir, "mbedtls"))
    rm_rf(os.path.join(install_dir, "mbedtls"))
    git_clone_shallow(
        "https://github.com/Mbed-TLS/mbedtls.git",
        version,
        os.path.join(source_dir, "mbedtls"),
    )
    with cd(os.path.join(source_dir, "mbedtls")):
        cmd(["python3", "scripts/config.py", "set", "MBEDTLS_SSL_DTLS_SRTP"])
        cmd(
            [
                "cmake",
                f"-DCMAKE_INSTALL_PREFIX={cmake_path(os.path.join(install_dir, 'mbedtls'))}",
                "-DCMAKE_BUILD_TYPE=Release",
                "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                "-B",
                os.path.join(build_dir, "mbedtls"),
            ]
            + cmake_args
        )
        cmd(
            [
                "cmake",
                "--build",
                os.path.join(build_dir, "mbedtls"),
                f"-j{multiprocessing.cpu_count()}",
                "--config",
                "Release",
            ]
        )
        cmd(["cmake", "--install", os.path.join(build_dir, "mbedtls")])


@versioned
def install_protobuf(version, source_dir, install_dir, platform: str):
    # platform:
    # - linux-aarch_64
    # - linux-ppcle_64
    # - linux-s390_64
    # - linux-x86_32
    # - linux-x86_64
    # - osx-aarch_64
    # - osx-universal_binary
    # - osx-x86_64
    # - win32
    # - win64
    url = f"https://github.com/protocolbuffers/protobuf/releases/download/v{version}/protoc-{version}-{platform}.zip"
    path = download(url, source_dir)
    rm_rf(os.path.join(install_dir, "protobuf"))
    extract(path, install_dir, "protobuf")
    # なぜか実行属性が消えてるので入れてやる
    for file in os.scandir(os.path.join(install_dir, "protobuf", "bin")):
        if file.is_file():
            os.chmod(file.path, file.stat().st_mode | stat.S_IXUSR)


@versioned
def install_protoc_gen_jsonif(version, source_dir, install_dir, platform: str):
    # platform:
    # - darwin-amd64
    # - darwin-arm64
    # - linux-amd64
    # - windows-amd64
    url = f"https://github.com/melpon/protoc-gen-jsonif/releases/download/{version}/protoc-gen-jsonif.tar.gz"
    rm_rf(os.path.join(source_dir, "protoc-gen-jsonif.tar.gz"))
    path = download(url, source_dir)
    jsonif_install_dir = os.path.join(install_dir, "protoc-gen-jsonif")
    rm_rf(jsonif_install_dir)
    extract(path, install_dir, "protoc-gen-jsonif")
    # 自分の環境のバイナリを <install-path>/bin に配置する
    shutil.copytree(
        os.path.join(jsonif_install_dir, *platform.split("-")),
        os.path.join(jsonif_install_dir, "bin"),
    )
    # なぜか実行属性が消えてるので入れてやる
    for file in os.scandir(os.path.join(jsonif_install_dir, "bin")):
        if file.is_file():
            os.chmod(file.path, file.stat().st_mode | stat.S_IXUSR)


@versioned
def install_libjpeg_turbo(version, source_dir, build_dir, install_dir, cmake_args):
    rm_rf(os.path.join(source_dir, "libjpeg-turbo"))
    rm_rf(os.path.join(build_dir, "libjpeg-turbo"))
    rm_rf(os.path.join(install_dir, "libjpeg-turbo"))
    git_clone_shallow(
        "https://github.com/libjpeg-turbo/libjpeg-turbo.git",
        version,
        os.path.join(source_dir, "libjpeg-turbo"),
    )
    mkdir_p(os.path.join(build_dir, "libjpeg-turbo"))
    with cd(os.path.join(build_dir, "libjpeg-turbo")):
        cmd(
            [
                "cmake",
                os.path.join(source_dir, "libjpeg-turbo"),
                f"-DCMAKE_INSTALL_PREFIX={os.path.join(install_dir, 'libjpeg-turbo')}",
                "-DCMAKE_BUILD_TYPE=Release",
            ]
            + cmake_args
        )
        cmd(
            [
                "cmake",
                "--build",
                os.path.join(build_dir, "libjpeg-turbo"),
                f"-j{multiprocessing.cpu_count()}",
                "--config",
                "Release",
            ]
        )
        cmd(["cmake", "--install", os.path.join(build_dir, "libjpeg-turbo")])


@versioned
def install_libyuv(version, source_dir, build_dir, install_dir, cmake_args):
    rm_rf(os.path.join(source_dir, "libyuv"))
    rm_rf(os.path.join(build_dir, "libyuv"))
    rm_rf(os.path.join(install_dir, "libyuv"))
    git_clone_shallow(
        "https://chromium.googlesource.com/libyuv/libyuv",
        version,
        os.path.join(source_dir, "libyuv"),
    )
    mkdir_p(os.path.join(build_dir, "libyuv"))
    with cd(os.path.join(build_dir, "libyuv")):
        cmd(
            [
                "cmake",
                os.path.join(source_dir, "libyuv"),
                f"-DCMAKE_INSTALL_PREFIX={os.path.join(install_dir, 'libyuv')}",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DCMAKE_PREFIX_PATH={os.path.join(install_dir, 'libjpeg-turbo')}",
            ]
            + cmake_args
        )
        cmd(
            [
                "cmake",
                "--build",
                os.path.join(build_dir, "libyuv"),
                f"-j{multiprocessing.cpu_count()}",
                "--config",
                "Release",
            ]
        )
        cmd(["cmake", "--install", os.path.join(build_dir, "libyuv")])


def install_deps(
    target_platform: str,
    build_platform: str,
    source_dir,
    shared_source_dir,
    build_dir,
    install_dir,
    debug,
):
    with cd(BASE_DIR):
        version = read_version_file("VERSION")

        # CMake
        install_cmake_args = {
            "version": version["CMAKE_VERSION"],
            "version_file": os.path.join(install_dir, "cmake.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
            "ext": "tar.gz",
        }
        if build_platform in ("windows_x86_64",):
            install_cmake_args["platform"] = "windows-x86_64"
            install_cmake_args["ext"] = "zip"
        elif build_platform in ("macos_x86_64", "macos_arm64"):
            install_cmake_args["platform"] = "macos-universal"
        elif build_platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
            install_cmake_args["platform"] = "linux-x86_64"
        elif build_platform in ("ubuntu-20.04_arm64", "ubuntu-22.04_arm64"):
            install_cmake_args["platform"] = "linux-aarch64"
        else:
            raise Exception("Failed to install CMake")
        install_cmake(**install_cmake_args)

        if build_platform == "macos_arm64":
            add_path(os.path.join(install_dir, "cmake", "CMake.app", "Contents", "bin"))
        else:
            add_path(os.path.join(install_dir, "cmake", "bin"))

        # libdatachannel
        dir = os.path.join(shared_source_dir, "libdatachannel")
        # url = "https://github.com/paullouisageneau/libdatachannel.git"
        url = "https://github.com/melpon/libdatachannel.git"
        if not os.path.exists(os.path.join(dir, ".git")):
            cmd(["git", "clone", url, dir])
            with cd(dir):
                cmd(["git", "checkout", "-f", version["LIBDATACHANNEL_VERSION"]])
                cmd(["git", "submodule", "update", "-i"])
                download(
                    "https://github.com/cisco/libsrtp/commit/91ceb8176afdbc5f2bdde0e409da011e99e22d9c.diff"
                )
                apply_patch(
                    "91ceb8176afdbc5f2bdde0e409da011e99e22d9c.diff", "deps/libsrtp", 1
                )

        # opus
        dir = os.path.join(shared_source_dir, "opus")
        url = "https://github.com/xiph/opus.git"
        if not os.path.exists(os.path.join(dir, ".git")):
            cmd(["git", "clone", url, dir])
            with cd(dir):
                cmd(["git", "checkout", "-f", version["OPUS_VERSION"]])

        # OpenH264
        install_openh264_args = {
            "version": version["OPENH264_VERSION"],
            "version_file": os.path.join(install_dir, "openh264.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
        }
        install_openh264(**install_openh264_args)

        # AOM
        install_aom_args = {
            "version": version["AOM_VERSION"],
            "version_file": os.path.join(install_dir, "aom.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": [],
        }
        if build_platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
            install_aom_args["cmake_args"] = [
                "-DCMAKE_C_COMPILER=clang-12",
                "-DCMAKE_CXX_COMPILER=clang++-12",
            ]
        install_aom(**install_aom_args)

        macos_cmake_args = []
        if build_platform in ("macos_x86_64", "macos_arm64"):
            sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            target = (
                "x86_64-apple-darwin"
                if target_platform in ("macos_x86_64",)
                else "aarch64-apple-darwin"
            )
            arch = "x86_64" if target_platform in ("macos_x86_64",) else "arm64"
            macos_cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
            macos_cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={arch}")
            macos_cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
            macos_cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
            macos_cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
            macos_cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")

        # MbedTLS
        install_mbedtls_args = {
            "version": version["MBEDTLS_VERSION"],
            "version_file": os.path.join(install_dir, "mbedtls.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": macos_cmake_args,
        }
        install_mbedtls(**install_mbedtls_args)

        # protobuf
        install_protobuf_args = {
            "version": version["PROTOBUF_VERSION"],
            "version_file": os.path.join(install_dir, "protobuf.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
        }
        if build_platform in ("windows_x86_64",):
            install_protobuf_args["platform"] = "win64"
        elif build_platform in ("macos_x86_64", "macos_arm64"):
            install_protobuf_args["platform"] = "osx-universal_binary"
        elif build_platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
            install_protobuf_args["platform"] = "linux-x86_64"
        else:
            raise Exception("Failed to install Protobuf")
        install_protobuf(**install_protobuf_args)

        # protoc-gen-jsonif
        install_jsonif_args = {
            "version": version["PROTOC_GEN_JSONIF_VERSION"],
            "version_file": os.path.join(install_dir, "protoc-gen-jsonif.version"),
            "source_dir": source_dir,
            "install_dir": install_dir,
            "platform": "",
        }
        if build_platform in ("windows_x86_64",):
            install_jsonif_args["platform"] = "windows-amd64"
        elif build_platform in ("macos_x86_64",):
            install_jsonif_args["platform"] = "darwin-amd64"
        elif build_platform in ("macos_arm64",):
            install_jsonif_args["platform"] = "darwin-arm64"
        elif build_platform in ("ubuntu-20.04_x86_64", "ubuntu-22.04_x86_64"):
            install_jsonif_args["platform"] = "linux-amd64"
        else:
            raise Exception("Failed to install protoc-gen-jsonif")
        install_protoc_gen_jsonif(**install_jsonif_args)

        # libjpeg-turbo
        install_libjpeg_turbo_args = {
            "version": version["LIBJPEG_TURBO_VERSION"],
            "version_file": os.path.join(install_dir, "libjpeg-turbo.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": macos_cmake_args,
        }
        install_libjpeg_turbo(**install_libjpeg_turbo_args)

        # libyuv
        install_libyuv_args = {
            "version": version["LIBYUV_VERSION"],
            "version_file": os.path.join(install_dir, "libyuv.version"),
            "source_dir": source_dir,
            "build_dir": build_dir,
            "install_dir": install_dir,
            "cmake_args": macos_cmake_args,
        }
        install_libyuv(**install_libyuv_args)


class LibVersion(object):
    sora_c_sdk: str
    sora_c_sdk_commit: str
    libdatachannel: str
    opus: str
    mbedtls: str
    nlohmann_json: str
    libjuice: str
    libsrtp: str
    plog: str
    usrsctp: str

    def to_cmake(self):
        return [
            f"-DSORA_C_SDK_VERSION={self.sora_c_sdk}",
            f"-DSORA_C_SDK_COMMIT={self.sora_c_sdk_commit}",
            f"-DLIBDATACHANNEL_VERSION={self.libdatachannel}",
            f"-DOPUS_VERSION={self.opus}",
            f"-DMBEDTLS_VERSION={self.mbedtls}",
            f"-DNLOHMANN_JSON_VERSION={self.nlohmann_json}",
            f"-DLIBJUICE_VERSION={self.libjuice}",
            f"-DLIBSRTP_VERSION={self.libsrtp}",
            f"-DPLOG_VERSION={self.plog}",
            f"-DUSRSCTP_VERSION={self.usrsctp}",
        ]

    @staticmethod
    def create(version, base_dir, libdatachannel_dir):
        libv = LibVersion()
        with cd(base_dir):
            libv.sora_c_sdk_commit = cmdcap(["git", "rev-parse", "HEAD"])
        with cd(libdatachannel_dir):
            # 以下のような出力が得られるので、ここから必要な部分を取り出す
            #  bc889afb4c5bf1c0d8ee29ef35eaaf4c8bef8a5d deps/json (bc889afb)
            #  5f753cad49059cea4eb492eb5c11a3bbb4dd6324 deps/libjuice (v1.3.3)
            #  a566a9cfcd619e8327784aa7cff4a1276dc1e895 deps/libsrtp (a566a9c)
            #  e21baecd4753f14da64ede979c5a19302618b752 deps/plog (e21baec)
            #  5ca29ac7d8055802c7657191325c06386640ac24 deps/usrsctp (5ca29ac)
            r = cmdcap(["git", "submodule", "status"])
            lines = r.split("\n")
            for line in lines:
                name, commit = line.strip().split(" ")[1:3]
                commit = commit.strip("()")
                if "/json" in name:
                    libv.nlohmann_json = commit
                elif "/libjuice" in name:
                    libv.libjuice = commit
                elif "/libsrtp" in name:
                    libv.libsrtp = commit
                elif "/plog" in name:
                    libv.plog = commit
                elif "/usrsctp" in name:
                    libv.usrsctp = commit
        libv.sora_c_sdk = version["SORA_C_SDK_VERSION"]
        libv.libdatachannel = version["LIBDATACHANNEL_VERSION"]
        libv.opus = version["OPUS_VERSION"]
        libv.mbedtls = version["MBEDTLS_VERSION"]
        return libv


AVAILABLE_TARGETS = [
    "windows_x86_64",
    "macos_x86_64",
    "macos_arm64",
    "ubuntu-20.04_x86_64",
    "ubuntu-22.04_x86_64",
    "ios",
    "android",
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("target", choices=AVAILABLE_TARGETS)
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--relwithdebinfo", action="store_true")
    parser.add_argument("--sumomo", action="store_true")
    parser.add_argument("--package", action="store_true")

    args = parser.parse_args()
    target_platform = args.target

    if args.package and not args.sumomo:
        print("You must specify --package together with --sumomo.")
        sys.exit(1)

    arch = platform.machine()
    if arch in ("AMD64", "x86_64"):
        arch = "x86_64"
    elif arch in ("aarch64", "arm64"):
        arch = "arm64"

    if target_platform in ("ubuntu-20.04_x86_64",):
        build_platform = "ubuntu-20.04_x86_64"
    elif target_platform in ("ubuntu-22.04_x86_64",):
        build_platform = "ubuntu-22.04_x86_64"
    elif target_platform in ("macos_x86_64", "macos_arm64"):
        build_platform = f"macos_{arch}"

    logging.info(f"Build platform: {build_platform}")
    logging.info(f"Target platform: {target_platform}")

    configuration = "debug" if args.debug else "release"
    dir = target_platform
    source_dir = os.path.join(BASE_DIR, "_source", dir, configuration)
    build_dir = os.path.join(BASE_DIR, "_build", dir, configuration)
    install_dir = os.path.join(BASE_DIR, "_install", dir, configuration)
    package_dir = os.path.join(BASE_DIR, "_package", dir, configuration)
    shared_source_dir = os.path.join(BASE_DIR, "_source")
    mkdir_p(source_dir)
    mkdir_p(build_dir)
    mkdir_p(install_dir)

    install_deps(
        target_platform,
        build_platform,
        source_dir,
        shared_source_dir,
        build_dir,
        install_dir,
        args.debug,
    )

    configuration = "Release"
    if args.debug:
        configuration = "Debug"
    if args.relwithdebinfo:
        configuration = "RelWithDebInfo"

    sorac_build_dir = os.path.join(build_dir, "sorac")
    mkdir_p(sorac_build_dir)
    with cd(sorac_build_dir):
        cmake_args = []
        cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
        cmake_args.append(
            f"-DCMAKE_INSTALL_PREFIX={cmake_path(os.path.join(install_dir, 'sorac'))}"
        )
        cmake_args.append(f"-DSORAC_TARGET={target_platform}")
        libver = LibVersion.create(
            read_version_file(os.path.join(BASE_DIR, "VERSION")),
            BASE_DIR,
            os.path.join(shared_source_dir, "libdatachannel"),
        )
        cmake_args += libver.to_cmake()
        cmake_args.append(
            f"-DPROTOBUF_DIR={cmake_path(os.path.join(install_dir, 'protobuf'))}"
        )
        cmake_args.append(
            f"-DPROTOC_GEN_JSONIF_DIR={cmake_path(os.path.join(install_dir, 'protoc-gen-jsonif'))}"
        )
        if target_platform in ("macos_x86_64", "macos_arm64"):
            sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
            target = (
                "x86_64-apple-darwin"
                if target_platform in ("macos_x86_64",)
                else "aarch64-apple-darwin"
            )
            arch = "x86_64" if target_platform in ("macos_x86_64",) else "arm64"
            cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
            cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={arch}")
            cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
            cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")
        if target_platform == "ios":
            cmake_args += ["-G", "Xcode"]
            cmake_args.append("-DCMAKE_SYSTEM_NAME=iOS")
            cmake_args.append("-DCMAKE_OSX_ARCHITECTURES=x86_64;arm64")
            cmake_args.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0")
            cmake_args.append("-DCMAKE_XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH=NO")
        if target_platform == "android":
            toolchain_file = os.path.join(
                install_dir, "android-ndk", "build", "cmake", "android.toolchain.cmake"
            )
            cmake_args.append(f"-DCMAKE_TOOLCHAIN_FILE={toolchain_file}")
            # cmake_args.append(f"-DANDROID_NATIVE_API_LEVEL={android_native_api_level}")
            # cmake_args.append(f"-DANDROID_PLATFORM={android_native_api_level}")
            cmake_args.append("-DANDROID_ABI=arm64-v8a")
            cmake_args.append("-DANDROID_CPP_FEATURES=exceptions rtti")
            # r23b には ANDROID_CPP_FEATURES=exceptions でも例外が設定されない問題がある
            # https://github.com/android/ndk/issues/1618
            cmake_args.append("-DCMAKE_ANDROID_EXCEPTIONS=ON")

        # NvCodec
        if target_platform in (
            "windows_x86_64",
            "ubuntu-20.04_x86_64",
            "ubuntu-22.04_x86_64",
        ):
            cmake_args.append("-DUSE_NVCODEC_ENCODER=ON")
            if target_platform == "windows_x86_64":
                cmake_args.append(
                    f"-DCUDA_TOOLKIT_ROOT_DIR={cmake_path(os.path.join(install_dir, 'cuda', 'nvcc'))}"
                )

        # oneVPL
        if target_platform in (
            "windows_x86_64",
            "ubuntu-20.04_x86_64",
            "ubuntu-22.04_x86_64",
        ):
            cmake_args.append("-DUSE_VPL_ENCODER=ON")
            cmake_args.append(
                f"-DVPL_ROOT_DIR={cmake_path(os.path.join(install_dir, 'vpl'))}"
            )

        # OpenH264
        cmake_args.append(
            f"-DOPENH264_ROOT_DIR={cmake_path(os.path.join(install_dir, 'openh264'))}"
        )

        # AOM
        cmake_args.append(
            f"-DAOM_ROOT_DIR={cmake_path(os.path.join(install_dir, 'aom'))}"
        )

        # libdatachannel
        cmake_args.append("-DUSE_MBEDTLS=ON")
        cmake_args.append(
            f"-DMbedTLS_ROOT={cmake_path(os.path.join(install_dir, 'mbedtls'))}"
        )
        cmake_args.append("-DUSE_GNUTLS=OFF")
        cmake_args.append("-DUSE_NICE=OFF")
        cmake_args.append("-DNO_TESTS=ON")
        cmake_args.append("-DNO_EXAMPLES=ON")

        # バンドルされたライブラリを消しておく
        # （CMake でうまく依存関係を解消できなくて更新されないため）
        rm_rf(os.path.join(sorac_build_dir, "bundled"))
        rm_rf(os.path.join(sorac_build_dir, "libsorac.a"))

        cmd(["cmake", BASE_DIR] + cmake_args)
        if target_platform == "ios":
            cmd(
                [
                    "cmake",
                    "--build",
                    ".",
                    f"-j{multiprocessing.cpu_count()}",
                    "--config",
                    configuration,
                    "--target",
                    "sorac",
                    "--",
                    "-arch",
                    "x86_64",
                    "-sdk",
                    "iphonesimulator",
                ]
            )
            cmd(
                [
                    "cmake",
                    "--build",
                    ".",
                    f"-j{multiprocessing.cpu_count()}",
                    "--config",
                    configuration,
                    "--target",
                    "sorac",
                    "--",
                    "-arch",
                    "arm64",
                    "-sdk",
                    "iphoneos",
                ]
            )
            # 後でライブラリは差し替えるけど、他のデータをコピーするためにとりあえず install は呼んでおく
            cmd(["cmake", "--install", "."])
            cmd(
                [
                    "lipo",
                    "-create",
                    "-output",
                    os.path.join(build_dir, "sorac", "libsorac.a"),
                    os.path.join(
                        build_dir,
                        "sorac",
                        f"{configuration}-iphonesimulator",
                        "libsorac.a",
                    ),
                    os.path.join(
                        build_dir, "sorac", f"{configuration}-iphoneos", "libsorac.a"
                    ),
                ]
            )
            shutil.copyfile(
                os.path.join(build_dir, "sorac", "libsorac.a"),
                os.path.join(install_dir, "sorac", "lib", "libsorac.a"),
            )
        else:
            cmd(
                [
                    "cmake",
                    "--build",
                    ".",
                    f"-j{multiprocessing.cpu_count()}",
                    "--config",
                    configuration,
                ]
            )
            cmd(["cmake", "--install", "."])

        # バンドルされたライブラリをインストールする
        if target_platform in ("windows_x86_64",):
            shutil.copyfile(
                os.path.join(sorac_build_dir, "bundled", "sorac.lib"),
                os.path.join(install_dir, "sora", "lib", "sorac.lib"),
            )
        else:
            shutil.copyfile(
                os.path.join(sorac_build_dir, "bundled", "libsorac.a"),
                os.path.join(install_dir, "sorac", "lib", "libsorac.a"),
            )

    if args.sumomo:
        if target_platform == "ios":
            # iOS の場合は事前に用意したプロジェクトをビルドする
            cmd(
                [
                    "xcodebuild",
                    "build",
                    "-project",
                    "examples/sumomo/ios/hello.xcodeproj",
                    "-target",
                    "hello",
                    "-arch",
                    "x86_64",
                    "-sdk",
                    "iphonesimulator",
                    "-configuration",
                    "Release",
                ]
            )
            # こっちは signing が必要になるのでやらない
            # cmd(['xcodebuild', 'build',
            #      '-project', 'examples/sumomo/ios/hello.xcodeproj',
            #      '-target', 'hello',
            #      '-arch', 'arm64',
            #      '-sdk', 'iphoneos',
            #      '-configuration', 'Release'])
        elif target_platform == "android":
            # Android の場合は事前に用意したプロジェクトをビルドする
            with cd(os.path.join(BASE_DIR, "examples", "sumomo", "android")):
                cmd(["./gradlew", "--no-daemon", "assemble"])
        else:
            # 普通のプロジェクトは CMake でビルドする
            sumomo_build_dir = os.path.join(build_dir, "examples", "sumomo")
            mkdir_p(sumomo_build_dir)
            with cd(sumomo_build_dir):
                cmake_args = []
                cmake_args.append(f"-DCMAKE_BUILD_TYPE={configuration}")
                cmake_args.append(
                    f"-DCMAKE_INSTALL_PREFIX={cmake_path(os.path.join(install_dir, 'sumomo'))}"
                )
                cmake_args.append(
                    f"-DSORAC_DIR={cmake_path(os.path.join(install_dir, 'sorac'))}"
                )
                cmake_args.append(f"-DSUMOMO_TARGET={target_platform}")
                cmake_args.append(
                    f"-DLIBJPEG_TURBO_DIR={cmake_path(os.path.join(install_dir, 'libjpeg-turbo'))}"
                )
                cmake_args.append(
                    f"-DLIBYUV_DIR={cmake_path(os.path.join(install_dir, 'libyuv'))}"
                )
                if target_platform in ("macos_x86_64", "macos_arm64"):
                    sysroot = cmdcap(["xcrun", "--sdk", "macosx", "--show-sdk-path"])
                    target = (
                        "x86_64-apple-darwin"
                        if target_platform in ("macos_x86_64",)
                        else "aarch64-apple-darwin"
                    )
                    arch = "x86_64" if target_platform in ("macos_x86_64",) else "arm64"
                    cmake_args.append(f"-DCMAKE_SYSTEM_PROCESSOR={arch}")
                    cmake_args.append(f"-DCMAKE_OSX_ARCHITECTURES={arch}")
                    cmake_args.append(f"-DCMAKE_C_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_CXX_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_OBJCXX_COMPILER_TARGET={target}")
                    cmake_args.append(f"-DCMAKE_SYSROOT={sysroot}")

                cmd(
                    ["cmake", os.path.join(BASE_DIR, "examples", "sumomo")] + cmake_args
                )
                cmd(
                    [
                        "cmake",
                        "--build",
                        ".",
                        f"-j{multiprocessing.cpu_count()}",
                        "--config",
                        configuration,
                    ]
                )
                cmd(["cmake", "--install", "."])

    if args.package:
        mkdir_p(package_dir)
        rm_rf(os.path.join(package_dir, "sorac"))
        rm_rf(os.path.join(package_dir, "sorac.env"))

        with cd(BASE_DIR):
            version = read_version_file("VERSION")
            sora_c_sdk_version = version["SORA_C_SDK_VERSION"]

        def archive(archive_path, files, is_windows):
            if is_windows:
                with zipfile.ZipFile(archive_path, "w") as f:
                    for file in files:
                        f.write(filename=file, arcname=file)
            else:
                with tarfile.open(archive_path, "w:gz") as f:
                    for file in files:
                        f.add(name=file, arcname=file)

        ext = (
            "zip"
            if target_platform in ("windows_x86_64", "windows_arm64")
            else "tar.gz"
        )
        is_windows = target_platform in ("windows_x86_64", "windows_arm64")
        content_type = "application/zip" if is_windows else "application/gzip"

        with cd(install_dir):
            archive_name = f"sora-c-sdk-{sora_c_sdk_version}_{target_platform}.{ext}"
            archive_path = os.path.join(package_dir, archive_name)
            archive(archive_path, enum_all_files("sorac", "."), is_windows)

            sumomo_archive_name = f"sumomo-{sora_c_sdk_version}_{target_platform}.{ext}"
            sumomo_archive_path = os.path.join(package_dir, sumomo_archive_name)
            archive(sumomo_archive_path, enum_all_files("sumomo", "."), is_windows)

            with open(os.path.join(package_dir, "sorac.env"), "w") as f:
                f.write(f"CONTENT_TYPE={content_type}\n")
                f.write(f"PACKAGE_NAME={archive_name}\n")
                f.write(f"SUMOMO_PACKAGE_NAME={sumomo_archive_name}\n")


if __name__ == "__main__":
    main()
