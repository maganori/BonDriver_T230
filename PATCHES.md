# このフォークでの変更点

本家 [hendecarows/BonDriver_T230](https://github.com/hendecarows/BonDriver_T230) に対する変更の一覧です。

## 1. チャンネル切り替え高速化パッチ（2026-11-22）

選局のたびに約1秒の待ちが入る問題を修正しました。原因は TS の受け渡しキューまわりの条件変数の取り違えと、スレッド終了待ちの詰まりです。

- `src/ts_sync_queue.cpp`
  - `SyncQueue::pop` が空き待ち用の条件変数 `cv_push_` を待っていたため、データが来ていても最大でタイムアウト（1,000 ms）まで返らなかった。正しくはデータ待ち用の `cv_pop_` を待つ。
- `src/tsmf_parser.cpp`
  - `TsmfParser::pop` も同様に `cv_pop_` を待つよう修正。
  - パススルー経路の push で、キューが空→非空に変わる際の `cv_pop_` 通知が欠けていたため追加。
- `src/usb_device.h`
  - `abort_stream_queue()` を追加。ストリーム用キューの abort フラグを立てる（次回 `start_stream` の `clear()` でリセットされる）。
- `BonDriver_T230/bon_driver.cpp`
  - `SetChannel` / `CloseTuner` で ParseTsmf スレッドを join する前に `abort_stream_queue()` を呼び、キュー待ちでブロックしたスレッドを起こしてから合流する（従来は平均 ~560 ms の追加待ち）。

### 実測（MyGica T230C v2 / CATV トランスモジュレーション、C62ch 459 MHz ⇔ C63ch 465 MHz）

| 項目 | 修正前 | 修正後 |
| --- | --- | --- |
| SetChannel | 889〜909 ms | 202〜226 ms |
| 最初の TS 到着 | ~1,900 ms | 263〜287 ms |
| OpenTuner | 2,642 ms | 2,497 ms（ファームウェア転送が支配的で不変） |

参考: 旧構成（BonDriver_T230C_C0 + LWire + 公式 BDA ドライバ）では SetChannel 2,214〜2,857 ms / 最初の TS 2,277〜2,905 ms でした。
分離済み TS の 300 秒録画で同期 100%、CC 不連続 0 を確認しています。

## 2. INF への CatalogFile 追加

- `inf/t230c.inf`: `CatalogFile=t230c.cat`
- `inf/t230a.inf`: `CatalogFile=t230a.cat`

自己署名したカタログで WinUSB ドライバパッケージを導入できるようにするための変更です。
カタログ（.cat）と証明書はリポジトリには含めていません（MakeCat + signtool で生成し、自己署名証明書で署名します）。
対応機種は本家どおり T230C（0572:C68A）と T230A（0572:689A）で、署名済みパッケージは両機種分を作成済みです。

## 3. tsmf_number 自動選択（2026-11-22）

`tsmf_number = -1`（未指定時の既定値でもある）を「自動」に再定義しました。従来は -1 と 0 はどちらもパススルーでしたが、自動でも TSMF でないストリームはパススルーになるため、既定の意味は実質そのままです。パススルーを強制したい場合は 0 を明示してください。

- 動作（`src/tsmf_parser.cpp` / `src/tsmf_parser.h`、変更箇所は "(patched)" 印）
  - 選局ごとに、最初の TSMF ヘッダ（CRC 検証済み）が読めた時点で、有効な相対 TS 番号のうち最小のものを選んで分離する
  - ヘッダが 512 パケット以内に現れなければ TSMF 多重ではないと判定してパススルーに切り替える
  - 判定は選局時に一度だけ行い、視聴中は追従しない。選んだ番号は情報ログに `tsmf auto: selected number = N` として残る
  - 1〜15 の明示指定と 0（パススルー強制）の意味は従来どおり
- テンプレート `BonDriver_T230.toml` の全チャンネルを `tsmf_number = -1` に変更し、意味のコメントを追加
