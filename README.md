# Sora C SDK

[![GitHub tag (latest SemVer)](https://img.shields.io/github/tag/shiguredo/sora-c-sdk.svg)](https://github.com/shiguredo/sora-c-sdk)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)

## About Shiguredo's open source software

We will not respond to PRs or issues that have not been discussed on Discord. Also, Discord is only available in Japanese.

Please read https://github.com/shiguredo/oss/blob/master/README.en.md before use.

## 時雨堂のオープンソースソフトウェアについて

利用前に https://github.com/shiguredo/oss をお読みください。

## Sora C SDK について

**現在開発中です**

[WebRTC SFU Sora](https://sora.shiguredo.jp/) 向けの C のクライアント向け SDK です。
[libwebrtc](https://webrtc.googlesource.com/src) を利用せず、
[libdatachannel](https://github.com/paullouisageneau/libdatachannel) を利用する事でバイナリサイズやフットプリント、アップデート頻度を抑えています。

## 特徴

- WebRTC ライブラリに libdatachannel を利用しています
  - アップデート頻度を抑えることができます
  - [paullouisageneau/libdatachannel](https://github.com/paullouisageneau/libdatachannel)
- コードのフットプリントが小さい Mbed TLS を利用しています
  - [Mbed\-TLS/mbedtl](https://github.com/Mbed-TLS/mbedtls)
- バイナリサイズが小さい
  - WebRTC と Sora シグナリングを含んだ libsorac は libwebrtc と比較してサイズは約 1/7 です
- Apache-2.0 ライセンスで OSS として公開しています
  - [Apache License, Version 2\.0](https://www.apache.org/licenses/LICENSE-2.0.html)
- H.265 (HEVC) 対応
  - ハードウェアアクセラレーターを利用
- OpenH264 対応
  - [cisco/openh264](https://github.com/cisco/openh264)
- RTCP Feedback Messages PLI 対応
  - [RFC 4585: Extended RTP Profile for Real\-time Transport Control Protocol \(RTCP\)\-Based Feedback \(RTP/AVPF\)](https://www.rfc-editor.org/rfc/rfc4585.html)
- Reduced-Size RTCP 対応
  - [RFC 5506: Support for Reduced\-Size Real\-Time Transport Control Protocol \(RTCP\): Opportunities and Consequences](https://www.rfc-editor.org/rfc/rfc5506)
- RTCP CNAME 対応
  - [RFC 3550: RTP: A Transport Protocol for Real\-Time Applications](https://www.rfc-editor.org/rfc/rfc3550.html)
  - [RFC 7022: Guidelines for Choosing RTP Control Protocol \(RTCP\) Canonical Names \(CNAMEs\)](https://www.rfc-editor.org/rfc/rfc7022)
- SCTP Zero Checksum 対応
  - [Zero Checksum for the Stream Control Transmission Protocol](https://datatracker.ietf.org/doc/html/draft-ietf-tsvwg-sctp-zero-checksum)

## Sora C++ SDK との比較

| 項目                        | Sora C++ SDK | Sora C SDK     |
| --------------------------- | ------------ | -------------- |
| ライセンス                  | Apache-2.0   | Apache-2.0     |
| WebRTC ライブラリ           | libwebrtc    | libdatachannel |
| WebRTC ライブラリライセンス | BSD-3-Clause | MPL-2.0        |
| バイナリサイズ              | 大きい       | 小さい         |
| フットプリント              | 大きい       | 小さい         |
| アップデート頻度            | 積極的       | 控えめ         |
| 暗号ライブラリ              | BoringSSL    | Mbed TLS       |
| SRTP ライブラリ             | libsrtp    | libsrtp        |
| SCTP ライブラリ             | libwebrtc (dcsctp)    | usrsctp        |
| ICE ライブラリ              | libwebrtc    | libjuice       |
| WebSocket ライブラリ        | Boost        | libdatachannel |

| プロトコル | Sora C++ SDK | Sora C SDK |
| ---------- | ------------ | ---------- |
| TURN-UDP   | 対応         | 対応       |
| TURN-TCP   | 対応         | 非対応     |
| TURN-TLS   | 対応         | 非対応     |

| コーデック  | Sora C++ SDK | Sora C SDK |
| ----------- | ------------ | ---------- |
| VP8         | 対応         | 優先実装   |
| VP9         | 対応         | 優先実装   |
| AV1         | 対応         | 優先実装   |
| H.264 (HWA) | 対応         | 対応       |
| H.265 (HWA) | 対応         | 対応       |

| OS / アーキテクチャ | Sora C++ SDK | Sora C SDK |
| ------------------- | ------------ | ---------- |
| linux / x86_64      | 対応         | 対応       |
| linux / x86         | 非対応       | 優先実装   |
| linux / arm64       | 対応         | 優先実装   |
| linux / arm         | 非対応       | 優先実装   |
| linux / riscv64     | 非対応       | 対応予定   |
| linux / riscv32     | 非対応       | 優先実装   |
| windows / x86_64    | 対応         | 優先実装   |
| windows / arm64     | 非対応       | 優先実装   |
| macos / arm64       | 対応         | 対応       |
| ios / arm64         | 対応         | 優先実装   |
| android / arm64     | 対応         | 優先実装   |

基本的には [Sora C++ SDK](https://github.com/shiguredo/sora-cpp-sdk) を利用してください。
バイナリサイズやフットプリント、アップデート頻度を抑えたい場合のみ Sora C SDK を利用してください。

### Sora 機能

| 機能                | Sora C++ SDK | Sora C SDK |
| ------------------- | ------------ | ---------- |
| sendrecv (送受信)   | 対応         | 非対応     |
| sendonly (送信のみ) | 対応         | 対応       |
| recvonly (受信のみ) | 対応         | 対応予定   |
| マルチストリーム    | 対応         | 対応       |
| サイマルキャスト    | 対応         | 対応予定   |
| スポットライト      | 対応         | 非対応     |

Sora C SDK は Sora の機能への積極な追従を行いません。

## 対応 Sora

WebRTC SFU Sora 2023.2.0 以降

### Sora Labo

検証目的であれば無料で利用可能な Sora Labo があります。
GitHub アカウントを持っていればすぐに利用可能です。

[Sora Labo](https://sora-labo.shiguredo.app/)

## 動作環境

- Ubuntu 22.04
  - x86_64
- Ubuntu 20.04
  - x86_64
- macOS 14
  - arm64
  - VideoToolbox
    - H.264 HWA (Encoder/Decoder)
    - H.265 HWA (Encoder/Decoder)

### 対応予定

- [VisionFive 2](https://www.starfivetech.com/en/site/boards)
  - Debian 12.0
  - riscv64
  - H.264 HWA (Decoder)
  - H.265 HWA (Encoder/Decoder)

## FAQ

[FAQ.md](doc/FAQ.md) をお読みください。

## 方針

- Sora の機能への積極的な追従は行いません
- libdatachannel へ積極的な貢献を行います
  - https://github.com/paullouisageneau/libdatachannel/pull/1009
  - https://github.com/paullouisageneau/libdatachannel/pull/1015
  - https://github.com/paullouisageneau/libdatachannel/pull/1037
  - https://github.com/paullouisageneau/libdatachannel/pull/1041
  - https://github.com/paullouisageneau/libdatachannel/pull/1061
- 映像コーデックは内蔵しません
  - 映像コーデックはハードウェアアクセラレーター、または外部コーデックライブラリを利用します

## 優先実装

優先実装とは Sora のライセンスを契約頂いているお客様限定で Sora C SDK の実装予定機能を有償にて前倒しで実装することです。

### 優先実装が可能な機能一覧

**詳細は Discord やメールなどでお気軽にお問い合わせください**

- [WebRTC's Statistics](https://www.w3.org/TR/webrtc-stats/)
  - libdatachannel 側への貢献を含みます
- [Google congestion control (GCC)](https://datatracker.ietf.org/doc/html/draft-alvestrand-rmcat-congestion-03)
  - libdatachannel 側への貢献を含みます
- [RTP Extensions for Transport-wide Congestion Control](https://datatracker.ietf.org/doc/html/draft-holmer-rmcat-transport-wide-cc-extensions-01)
  - [Transport-Wide Congestion Control](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/transport-wide-cc-02)
  - libdatachannel 側への貢献を含みます
- [RTCP XR](https://datatracker.ietf.org/doc/html/rfc3611)
  - libdatachannel 側への貢献を含みます
- [RTP Retransmission Payload Format](https://datatracker.ietf.org/doc/html/rfc4588)
  - libdatachannel 側への貢献を含みます
- [RTP ULPFEC](https://datatracker.ietf.org/doc/html/rfc5109)
  - libdatachannel 側への貢献を含みます
- [RTP FlexFEC](https://datatracker.ietf.org/doc/html/rfc8627)
  - libdatachannel 側への貢献を含みます
- RTP header extensions
  - [Absolute Capture Time](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/abs-capture-time/)
  - [Absolute Send Time](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/abs-send-time/)
  - [Video Layers Allocation](https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/rtp-hdrext/video-layers-allocation00/)
  - libdatachannel 側への貢献を含みます
- HWA を利用したコーデック
  - AAC HWA 対応
    - Sora の対応も含みます
    - [RTP Payload Format for MPEG\-4 Audio/Visual Streams](https://datatracker.ietf.org/doc/html/rfc6416)
  - VP8 / VP9 / AV1 / H.264 / H.265 の HWA 対応
    - AMD [Advanced Media Framework (AMF) SDK](https://github.com/GPUOpen-LibrariesAndSDKs/AMF)
    - Intel [oneVPL](https://github.com/oneapi-src/oneVPL)
    - NVIDIA [Video Codec SDK](https://developer.nvidia.com/video-codec-sdk)
    - NVIDIA [Jetson](https://www.nvidia.com/ja-jp/autonomous-machines/embedded-systems/)
- 次世代コーデック
  - H.266 対応
    - Sora の対応も含みます
    - libdatachannel 側への貢献を含みます
    - [RTP Payload Format for Versatile Video Coding \(VVC\)](https://datatracker.ietf.org/doc/html/rfc9328)
  - EVC 対応
    - Sora の対応も含みます
    - libdatachannel 側への貢献を含みます
    - [RTP Payload Format for Essential Video Coding \(EVC\)](https://datatracker.ietf.org/doc/html/draft-ietf-avtcore-rtp-evc)
- プラットフォーム
  - iOS 対応
  - Android 対応
  - [Raspberry Pi OS 対応](https://www.raspberrypi.com/software/)
    - arm64
    - arm
  - [Windows IoT 対応](https://learn.microsoft.com/ja-jp/previous-versions/windows/iot-core/windows-iot)
    - x86
    - arm64
  - [Ubuntu Core 対応](https://ubuntu.com/core)
    - x86
    - arm64
    - arm

## サポートについて

### Discord

- **サポートしません**
- アドバイスします
- フィードバック歓迎します

最新の状況などは Discord で共有しています。質問や相談も Discord でのみ受け付けています。

https://discord.gg/shiguredo

### バグ報告

Discord へお願いします。

## ライセンス

Apache License 2.0

```
Copyright 2023-2023, Wandbox LLC (Original Author)
Copyright 2023-2023, Shiguredo Inc.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```

## OpenH264

https://www.openh264.org/BINARY_LICENSE.txt

```
"OpenH264 Video Codec provided by Cisco Systems, Inc."
```
