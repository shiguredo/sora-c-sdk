#include "sorac/version.hpp"

#include <fstream>
#include <sstream>
#include <vector>

#include "util.hpp"
#include "version.gen.h"

#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

#if defined(__APPLE__)
#include "mac_version.hpp"
#endif

namespace sorac {

std::string Version::GetClientName() {
  return "Sora C SDK " SORA_C_SDK_VERSION " (" SORA_C_SDK_COMMIT ")";
}

std::string Version::GetEnvironment() {
  std::string os = "Unknown OS";
  std::string osver = "Unknown Version";
  std::string arch = "Unknown Arch";

#if defined(WIN32)
  os = "Windows";

  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  switch (sysInfo.wProcessorArchitecture) {
    // x64 (AMD or Intel)
    case PROCESSOR_ARCHITECTURE_AMD64:
      arch = "x64";
      break;
    // ARM
    case PROCESSOR_ARCHITECTURE_ARM:
      arch = "arm";
      break;
    // ARM64
    case PROCESSOR_ARCHITECTURE_ARM64:
      arch = "arm64";
      break;
    // Intel Itanium-based
    case PROCESSOR_ARCHITECTURE_IA64:
      arch = "IA64";
      break;
    // x86
    case PROCESSOR_ARCHITECTURE_INTEL:
      arch = "x86";
      break;
    case PROCESSOR_ARCHITECTURE_UNKNOWN:
    default:
      arch = "unknown";
      break;
  }

  HMODULE module = GetModuleHandleW(L"ntdll.dll");
  if (module != nullptr) {
    typedef int(WINAPI * RtlGetVersionFunc)(LPOSVERSIONINFOW);

    auto rtl_get_version =
        (RtlGetVersionFunc)GetProcAddress(module, "RtlGetVersion");
    if (rtl_get_version != nullptr) {
      OSVERSIONINFOW versionInfo = {sizeof(OSVERSIONINFOW)};
      auto status = rtl_get_version(&versionInfo);
      if (status == 0) {
        osver = std::to_string(versionInfo.dwMajorVersion) + "." +
                std::to_string(versionInfo.dwMinorVersion) + " Build " +
                std::to_string(versionInfo.dwBuildNumber);
      }
    }
  }

#endif

#if defined(__APPLE__) || defined(__linux__)
  struct utsname u;
  int r = uname(&u);
  if (r == 0) {
    arch = u.machine;
  }
#endif

#if defined(__APPLE__)
  os = MacVersion::GetOSName();
  osver = MacVersion::GetOSVersion();
#else
  // /etc/os-release ファイルを読んで PRETTY_NAME を利用する

  // /etc/os-release は以下のような内容になっているので、これを適当にパースする
  /*
      $ docker run -it --rm ubuntu cat /etc/os-release
      NAME="Ubuntu"
      VERSION="18.04.3 LTS (Bionic Beaver)"
      ID=ubuntu
      ID_LIKE=debian
      PRETTY_NAME="Ubuntu 18.04.3 LTS"
      VERSION_ID="18.04"
      HOME_URL="https://www.ubuntu.com/"
      SUPPORT_URL="https://help.ubuntu.com/"
      BUG_REPORT_URL="https://bugs.launchpad.net/ubuntu/"
      PRIVACY_POLICY_URL="https://www.ubuntu.com/legal/terms-and-policies/privacy-policy"
      VERSION_CODENAME=bionic
      UBUNTU_CODENAME=bionic
    */
  // 行ごとに分けたデータを取得
  std::vector<std::string> lines;
  {
    std::stringstream ss;
    std::ifstream fin("/etc/os-release");
    ss << fin.rdbuf();
    std::string content = ss.str();
    lines = split_with(ss.str(), "\n");
  }
  const std::string PRETTY_NAME = "PRETTY_NAME=";
  for (auto& line : lines) {
    // 先頭が PRETTY_NAME= の行を探す
    if (line.find(PRETTY_NAME) != 0) {
      continue;
    }
    // PRETTY_NAME= 以降のデータを取り出す
    os = line.substr(PRETTY_NAME.size());
    // 左右の " を除ける
    os = trim(os, "\"");
    // os にバージョン情報も含まれてしまったので、osver には空文字を設定しておく
    osver = "";
    break;
  }
#endif

  std::string env;
  env += os;
  if (!osver.empty()) {
    env += " " + osver;
  }
  if (!arch.empty()) {
    env += " [" + arch + "]";
  }
  env += std::string(" / ") + SORAC_ALL_VERSION;
  return env;
}

}  // namespace sorac