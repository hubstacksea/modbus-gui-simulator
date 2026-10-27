# Modbus GUI — Modbus マスターツール / スレーブシミュレータ

**libmodbus** と **wxWidgets** で作った Modbus の GUI ツールです。
1 つのウィンドウでマスター（クライアント）タブとスレーブ（サーバ・シミュレータ）タブを好きな数だけ開けます。
Linux（GTK3）と Windows（MinGW-w64：クロスコンパイルまたは MSYS2 ネイティブ）の両方でビルドできます。

## 機能

### マスター（クライアント）
- Modbus TCP / Modbus RTU に対応（ボーレート、データビット、パリティ、ストップビット、Linux の RS485 モード）
- 応答タイムアウトとバイト間タイムアウトを設定可能
- 読み出し：FC01 / 02 / 03 / 04。単発読み出しと周期ポーリング（間隔 ms 指定）
- 書き込み：FC05 / 06 / 15 / 16 / 22（Mask Write）/ 23（Read/Write Multiple）
- テーブルのセルをダブルクリックしてその値を書き込み（コイルは反転、レジスタは入力ダイアログ）
- 表示形式：Unsigned / Signed / Hex / Binary 16、U/S/Float 32、U/S/Float 64、ASCII
- バイトオーダー：ABCD / CDAB / BADC / DCBA
- FC17 Report Server ID
- Raw リクエスト（任意の PDU を 16 進で送信。MBAP ヘッダ / CRC は自動付加）
- ユニット ID スキャン（指定範囲で応答するデバイスを検出）
- 通信ログ（色分け、一時停止、保存）と統計（Tx/OK/Err、応答時間）
- TCP 切断時の自動再接続

### スレーブ（サーバ・シミュレータ）
- Modbus TCP サーバ（複数クライアント同時接続）/ Modbus RTU スレーブ
- コイル、ディスクリート入力、入力レジスタ、保持レジスタの全アドレス 0..65535
- テーブル上で直接編集（値 / 16 進 / 名前・コメント）。表示形式とバイトオーダーは切り替え可能
- マスターから書き込まれたセルを黄色でハイライト
- アドレスごとの値シミュレーション：Increment / Decrement / Random / Sine / Triangle / Sawtooth / Toggle
  （32/64 ビット値や浮動小数点値は、選んだ形式とバイトオーダーで生成）
- 異常系の注入：応答遅延、無応答、例外の強制返答（01〜0B）
- ユニット ID フィルタ（TCP では「全ユニット ID に応答」も選択可）
- 受信リクエストのログ（接続元、ファンクション、アドレス、生フレーム）
- レジスタ値・名前・シミュレーション設定を CSV で保存 / 読み込み

## ビルド

libmodbus 3.1.11 を `third_party/libmodbus` に同梱しており、CMake がビルドして静的リンクします（autotools は不要）。

### Linux
```sh
sudo apt install build-essential cmake libwxgtk3.2-dev
./build_linux.sh            # -> build-linux/modbus_gui
```

### Windows（Linux 上での MinGW-w64 クロスビルド）
MinGW 向けにビルドした wxWidgets が必要です（静的ビルドを推奨）。
```sh
sudo apt install mingw-w64 cmake
# wxWidgets を MinGW 向けにビルドする例:
#   ../configure --host=x86_64-w64-mingw32 --disable-shared --prefix=$HOME/wx-mingw
#   make -j && make install
WX_MINGW_DIR=$HOME/wx-mingw ./build_mingw.sh   # -> build-mingw/modbus_gui.exe
```
静的な wxWidgets を使うと、exe はシステム DLL 以外に依存しません（exe 1 つでそのまま動きます）。

### Windows（MSYS2 の MinGW64 シェルでネイティブビルド）
```sh
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake make mingw-w64-x86_64-wxwidgets3.2-msw
cmake -S . -B build -G "MSYS Makefiles"
cmake --build build
```

## パッケージ作成

成果物は `dist/` に出力されます（CPack を使用）。

| 対象 | コマンド | 出力 |
|---|---|---|
| Linux | `./package_linux.sh` | `modbus-gui_<ver>-1_amd64.deb`, `modbus-gui-<ver>-1.x86_64.rpm` |
| Windows | `WX_MINGW_DIR=$HOME/wx-mingw ./package_windows.sh` | `modbus-gui-<ver>-win64.7z` |

- Linux パッケージは `/usr/bin/modbus_gui`、デスクトップエントリ（`modbus-gui.desktop`）、
  hicolor アイコン（16〜512px）、ドキュメントをインストールします。
  依存関係は DEB では dpkg-shlibdeps、RPM では rpmbuild が自動で生成します。
  必要なもの：`dpkg-dev`、`rpm`（rpmbuild）。
- RPM はビルドしたマシンのライブラリ（glibc / wxWidgets 3.2）に対してリンクされます。
  対象のディストリビューションで動かすには、そのディストリ上（または同等の環境）でビルドしてください。
- Windows の 7z には exe 単体（静的リンク、アイコンとバージョン情報を埋め込み済み）、README、
  libmodbus のライセンスが入っています。展開すればそのまま実行できます。
- メンテナ / ライセンス / URL は CMake 変数で指定します。
  ```sh
  ./package_linux.sh -DMODBUSGUI_MAINTAINER="Name <mail@example.com>" \
                     -DMODBUSGUI_LICENSE="MIT" -DMODBUSGUI_HOMEPAGE="https://..."
  ```
- バージョンは `CMakeLists.txt` の `project(ModbusGui VERSION x.y.z)` で決まります
  （exe のバージョン情報とパッケージ名に反映されます）。

## アイコン

`tools/make_icons.py`（Pillow を使用）でアイコン一式を生成します。生成済みのファイルはリポジトリに含まれています。
- `resources/icons/modbus-gui-<N>.png`：Linux の hicolor テーマ用
- `resources/modbus_gui.ico`：Windows の exe アイコン（`src/app.rc` で埋め込み）
- `src/AppIconData.h`：ウィンドウアイコン用の PNG を埋め込んだデータ（両 OS 共通）

デザインを変えたら `python3 tools/make_icons.py` を実行して再生成してください。

## テスト
ヘッドレスの結合テスト（マスター⇔スレーブ間の通信、データ形式変換）を用意しています。
```sh
cmake -S . -B build-linux -DMODBUSGUI_BUILD_TESTS=ON && cmake --build build-linux
./build-linux/test_core
# RTU テスト（仮想シリアルペアを使用）
python3 tests/pty_bridge.py      # 2 つのデバイス名を表示（例: /dev/pts/4 /dev/pts/5）
MODBUS_TEST_RTU_A=/dev/pts/4 MODBUS_TEST_RTU_B=/dev/pts/5 ./build-linux/test_core
```

## 注意事項
- Linux で TCP サーバに 502 番ポートを使う場合、root 権限か `CAP_NET_BIND_SERVICE` が必要になることがあります
  （テストには 1502 番などを使ってください）。
- シリアルポートの一覧：Linux は `/dev/ttyUSB*`、`ttyACM*`、`ttyS*` などを、Windows はレジストリから COM ポートを列挙します。
  コンボボックスには直接入力もできます（例：`/dev/pts/3`、`COM12`）。
- RTU スレーブが他ユニット宛てのリクエストを無視したあと、libmodbus は次のフレームをそのユニットの応答とみなして読み捨てます
  （最大 200 ms）。
- libmodbus のライセンスは LGPL-2.1-or-later です。静的リンクしたバイナリを配布するときは、LGPL の条件に従ってください
  （オブジェクトファイルやソースを提供して、利用者が再リンクできるようにするなど）。

## 構成
```
src/App.cpp, MainFrame.*      アプリ本体とタブ管理
src/MasterPanel.*             マスター UI
src/MasterWorker.*            マスター通信スレッド（キュー経由のリクエスト、ポーリング）
src/SlavePanel.*              スレーブ UI（仮想グリッド、シミュレーション、CSV）
src/SlaveServer.*             スレーブ通信スレッド（TCP マルチクライアント select / RTU）
src/ConnectionPanel.*         TCP/RTU 接続設定ウィジェット
src/DataFormat.*              値の形式変換
src/ModbusUtil.*              libmodbus ヘルパ、シリアルポート列挙
src/LogView.*                 ログ表示
src/UiUtil.h                  UI 共通ヘルパ（スピンコントロールの幅、テーマ対応の色）
src/AppIconData.h             埋め込みアイコン（自動生成）
src/app.rc                    Windows リソース（アイコン、バージョン情報、マニフェスト）
resources/                    アイコン（PNG / ICO）
packaging/linux/              .desktop ファイル
tools/make_icons.py           アイコン生成スクリプト
package_linux.sh              .deb / .rpm の作成
package_windows.sh            .7z の作成（MinGW クロスビルド）
cmake/                        同梱 libmodbus のビルド、wx-config の検出、MinGW ツールチェーン、パッケージ設定
third_party/libmodbus         libmodbus 3.1.11（未改変）
```
