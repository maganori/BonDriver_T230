# BonDriver_T230

[MyGica T230C/A][link_t230]用のWindows版BonDriverです。以下のデバイスに対応しています。

| Device | Vid    | Pid    |
| :----: | :----: | :----: |
| T230C  | 0x0572 | 0xc68a |
| T230A  | 0x0572 | 0x689a |

## インストール

### ビルド

リポジトリをクローンし、[Visual Studio 2022][link_vs]でディレクトリ内の`BonDriver_T230.sln`を開いてビルドします。

* [Visual Studio 2022][link_vs]
* [plog][link_plog]
* [toml11][link_toml11]

```console
git clone https://github.com/hendecarows/BonDriver_T230.git
cd BonDriver_T230
git submodule update --init --recursive
```

### ドライバのインストール

ドライバー署名の強制を無効にした状態でINFファイルを使用しWinUSBドライバをインストールします。

#### ドライバー署名の強制を無効にする方法

1. スタートメニューからキーボードのShiftキーを押しながら再起動をクリックします
2. 再起動後に表示されるメニューから以下の順に選択し再起動します
   1. トラブルシューティング
   2. 詳細オプション
   3. スタートアップ設定
   4. 再起動
3. 再起動後に表示されるオプション一覧から以下を選択します
   * 7)ドライバー署名の強制を無効にする

#### ドライバーのインストール方法

1. デバイスマネージャーを起動します
2. ほかのデバイス
3. USB Stickを右クリック
4. ドライバーの更新
5. コンピューターを参照してドライバーを検索
6. 参照ボタンからinfファイルのあるディレクトリを指定
7. このドライバーソフトウェアをインストールします
8. サウンド、ビデオ、およびゲーム コントロール
   * MyGica T230C DVB-C Receiver Device (WinUSB)
   * MyGica T230A DVB-C Receiver Device (WinUSB)

### ファームウェア

ファームウェアを[osmc][link_osmc]から入手し、`BonDriver_T230.dll`と同じディレクトリに配置します。

* [dvb-demod-si2168-d60-01.fw][link_demodfw]
* [dvb-tuner-si2141-a10-01.fw][link_tunerfw]

### 設定ファイル

`BonDriver_T230.dll`と同じディレクトリに[TOML][link_toml]形式の設定ファイル`BonDriver_T230.toml`を配置します。
`BonDriver_T230.toml`は付属のものを使うのではなく、必要なチャンネルのみを抜粋して新規に作成することをおすすめします。

```toml
[device]
# デバイス設定

# ログレベルを設定
# none = 0, fatal = 1, error = 2, warning = 3, info = 4, debug = 5, verbose = 6
# DLL内でログレベル以上の出力があった場合に拡張子.logのログファイルが作成される
log.level = 2

# USBデバイスのGUIDを設定
# ドライバのインストールに使用したINFファイル内の値
# t230a.inf
# guid = "{3ba7eb50-f570-4356-977b-c7242157543c}"
# t230c.inf
guid = "{00347aaf-38dc-4522-9a32-13957dafec54}"

# ファームウェアのファイル名を設定
# ファームウェアはBonDriverと同じディレクトリに配置
# Demodulator用
demodulator.firmware = "dvb-demod-si2168-d60-01.fw"
# Tuner用
tuner.firmware = "dvb-tuner-si2141-a10-01.fw"

# TS転送に用いるUSBリクエスト数、転送サイズ、キューサイズ
# ドロップ発生状況に応じて調整するのが良いが調整してもゼロになる訳ではない
stream.request.size = 16
stream.transfer.size = 8192
stream.queue.size = 64

# チューニング空間設定
[[space]]

# チューニング空間名
name = "TransModulation"

# チューニング空間内のチャンネル設定
[[space.channel]]

# チャンネル名
name = "C47ch"

# チャンネル周波数(Hz)
frequency = 369000000

# TSMF分離設定
# 残す相対ストリーム番号(1-15)をtsmf_numberに設定する
# tsmf_numberが設定されていないか範囲外の場合は分離しない
tsmf_number = 1

# 必要なチャンネル設定を順に[[space.channel]]から追加する
[[space.channel]]
name = "C48ch"
frequency = 375000000
tsmf_number = 2


# 複数のチューニング空間を定義したい場合は[[space]]から同様に定義する
[[space]]
name = "UHF"

[[space.channel]]
name = "13ch"
frequency = 473000000
tsmf_number = 0
```

視聴可能なチャンネルは、[TVTest][link_tvtest]を使用して確認することができます。
`BonDriver_T230.dll`と付属の`BonDriver_T230.toml`がTVTestのBonDriverを配置するディレクトリに存在し、使用可能な状態になってるものとします。

1. TVTestを起動し、「設定」「チャンネルスキャン」から全ての「対象チューニング空間」に対してチャンネルスキャンを実行します
2. 「設定」を終了し、マウス右クリックのメニューから「ストリーム情報」「PID」タブを表示します
3. 必要とするチャンネルに変更します
4. 「ストリーム情報」の「PID」に`002f`が存在するか確認します
   1. 存在する場合 TSMF 形式です
   2. 「設定」「チャンネルスキャン」「チャンネル一覧」「チャンネル」からBonDriverのチャンネル名を確認します
   3. 必要とするチャンネルに対して、3. 4.を繰り返します
5. TVTestを終了し、`BonDriver_T230.toml`をエディタで開き、4.で確認したチャンネルの`tsmf_number = 0`を`tsmf_number = 1`に変更します
6. TVTestを起動し、5. のチャンネルが正常に視聴出来ることを確認します
7. 視聴できない場合は、`tsmf_number = 2`から1ずつ増やしながら最大15まで確認します

## 謝辞

このソフトウェアは以下を基に作成しました。作者の方々に感謝します。

* T230の制御方法
  * Linux DVBドライバのソースファイル
    * [dvbsky.c][link_dvbsky]
    * [si2168.c][link_si2168]
    * [si2157.c][link_si2157]
* WinUSB関数を使用したデバイスへのアクセス方法
  * [px4_drv][link_px4drv]のソースファイル
    * [device_base.cpp][link_px4drv2]
    * [device_manager.cpp][link_px4drv3]
    * [itedtv_bus_winusb.c][link_px4drv4]
* TSMF(transport streams multiplexing frame)への対応方法
  * あきーのブログ
    * [2021-03-24 MPEG-TS多重化（TSMF）の仕組みとbondriver_BDA等における実際のTSMF対応実装][link_blog]
  * [BonDriver_BDA][link_bonbda]のソースファイル
    * [TSMF.cpp][link_tsmfcpp]

## ライセンス

GNU General Public License Version 2.0

[link_t230]: https://www.mygica.com/product/t230/
[link_vs]: https://visualstudio.microsoft.com/ja/vs/

[link_plog]: https://github.com/SergiusTheBest/plog
[link_toml11]: https://github.com/ToruNiina/toml11
[link_toml]: https://toml.io/ja/v1.0.0

[link_demodfw]: https://github.com/osmc/dvb-firmware-osmc/raw/master/dvb-demod-si2168-d60-01.fw
[link_tunerfw]: https://github.com/osmc/dvb-firmware-osmc/raw/master/dvb-tuner-si2141-a10-01.fw
[link_osmc]: https://github.com/osmc/dvb-firmware-osmc

[link_tvtest]: https://github.com/DBCTRADO/TVTest

[link_dvbsky]: https://github.com/torvalds/linux/blob/master/drivers/media/usb/dvb-usb-v2/dvbsky.c
[link_si2168]: https://github.com/torvalds/linux/blob/master/drivers/media/dvb-frontends/si2168.c
[link_si2157]: https://github.com/torvalds/linux/blob/master/drivers/media/tuners/si2157.c

[link_px4drv]: https://github.com/nns779/px4_drv
[link_px4drv2]: https://github.com/nns779/px4_drv/blob/develop/winusb/src/DriverHost_PX4/device_base.cpp
[link_px4drv3]: https://github.com/nns779/px4_drv/blob/develop/winusb/src/DriverHost_PX4/device_manager.cpp
[link_px4drv4]: https://github.com/nns779/px4_drv/blob/develop/winusb/src/DriverHost_PX4/itedtv_bus_winusb.c

[link_blog]: https://akkkix.hatenablog.com/entry/2021/03/24/041254
[link_bonbda]: https://github.com/radi-sh/BonDriver_BDA
[link_tsmfcpp]: https://github.com/radi-sh/BonDriver_BDA/blob/master/BonDriver_BDA/TSMF.cpp
