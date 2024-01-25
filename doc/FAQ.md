# FAQ

## ビルドに関する質問

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-c-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

## Sora C SDK はどうやってビルドすればいいですか？

```bash
$ python3 run.py ubuntu-22.04_x86_64
```

```bash
$ python3 run.py macos_arm64
```

## サンプルの Sumomo はどうやってビルドすればいいですか？

```bash
$ python3 run.py ubuntu-22.04_x86_64 --sumomo
```

```bash
$ python3 run.py macos_arm64 --sumomo
```

## 送受信 (sendrecv) に対応予定はありますか？

送信のみ (sendonly) と 受信のみ (recvonly) の対応になります。

優先実装として送受信 (sendrecv) への対応を予定しています。

## Sumomo 利用時に OS 組み込みの証明書を利用できますか？

Sumomo にある `--cacert` に指定する事で利用可能です。

### Ubuntu

`ca-certificates` をインストールしてください。

```bash
$ apt install ca-certificates
```

以下を `--cacert` で指定してください。

`/etc/ssl/certs/ca-certificates.crt`

### macOS

ルート証明書を生成して `--cacert` で指定してください。

```bash
$ /usr/bin/security export -t certs -f pemseq -k /System/Library/Keychains/SystemRootCertificates.keychain > cacert.pem
```

## 帯域が不安定になった場合にビットレートを自動で下げてくれますか？

Sora C SDK で利用している libdatachannel ではメディアトランスポートの輻輳制御の機能が搭載されていません。

libwebrtc ベースの [Sora C++ SDK](https://github.com/shiguredo/sora-cpp-sdk) を利用してください。

## サイマルキャストに対応していますか？

対応しています。 `--simulcast` で指定してください。

ただし、ビットレートや解像度の動的な変更には対応していません。

それらをご利用になりたい場合は libwebrtc ベースの [Sora C++ SDK](https://github.com/shiguredo/sora-cpp-sdk) を利用してください。

## サイマルキャストのエンコーディングパラメーターのカスタマイズはできますか？

対応しているパラメーターは以下です。`maxFramerate` と `adaptivePtime`、 `scalabilityMode` には対応していません。

- rid
- active
- scaleResolutionDownBy
- maxBitrate

## サイマルキャストエンコーディングパラメータの `active: false` にすることで映像を ３ 本送信しないことはできますか？

可能です。

## Sumomo の使い方を教えてください

### macOS arm64

```bash
./sumomo \
    --signaling-url wss://sora.example.com/signaling \
    --channel-id sumomo \
    --capture-type mac \
    --video-codec-type H265 \
    --h265-encoder-type videotoolbox \
    --audio-type macos \
    --cacert cacert.pem
```

## Sumomo のヘルプ

```bash
$ ./sumomo --help
Usage: ./sumomo [options]
Options:
  --signaling-url=URL [required]
  --channel-id=ID [required]
  --simulcast=true,false,none
  --video-codec-type=H264,H265
  --video-bit-rate=0-5000 [kbps]
  --metadata=JSON
  --capture-type=fake,v4l2,mac
  --capture-device-name=NAME
  --capture-device-width=WIDTH
  --capture-device-height=HEIGHT
  --audio-type=fake,pulse,macos
  --h264-encoder-type=openh264,videotoolbox
  --h265-encoder-type=videotoolbox
  --openh264=PATH
  --cacert=PATH
  --help
```
