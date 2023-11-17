# FAQ

## ビルドに関する質問

ビルド関連の質問については環境問題がほとんどであり、環境問題を改善するコストがとても高いため基本的には解答しません。

GitHub Actions でビルドを行い確認していますので、まずは GitHub Actions の [build.yml](https://github.com/shiguredo/sora-c-sdk/blob/develop/.github/workflows/build.yml) を確認してみてください。

GitHub Actions のビルドが失敗していたり、
ビルド済みバイナリがうまく動作しない場合は Discord へご連絡ください。

## サンプルである Sumomo はどうやってビルドしますか？

```bash
$ python3 run.py macos_arm64 --sumomo
```

```bash
$ python3 run.py ubuntu-22.04_x86_64 --sumomo
```

## 送受信 (sendrecv) に対応予定はありますか？

送信のみ (sendonly) と 受信のみ (recvonly) の対応になります。

優先実装として送受信 (sendrecv) への対応を予定しています。
