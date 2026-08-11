# 打牌期待値解析ツール

保存した牌譜を渡すと、対象プレイヤーの打牌すべてについて

- その打牌の**期待値**と、全打牌候補の中での**期待値順位**
- **期待値を最大化する打牌**（最善手）と、実際の打牌との差（損失）

をコンソールと HTML レポートに出力します。

このツールは [nekobean/mahjong-cpp](https://github.com/nekobean/mahjong-cpp) の
フォークで追加したものです。期待値の計算そのものは本家の `ExpectedScoreCalculator` を
そのまま使っており、このツールが担っているのは「牌譜を読んで局面を復元し、各打牌局面を
計算器にかけて結果を並べる」部分です。

```bash
analyze_majsoul_paipu 牌譜.html --html report.html
```

実行ファイルに牌譜ファイルをドラッグ&ドロップするだけでも解析できます。

## 対応する入力

| 形式 | 説明 |
| --- | --- |
| `.html` | [mjai-reviewer](https://mjai.ekyu.moe/) の HTML レポート。各局の牌譜が天鳳 JSON 形式で埋め込まれているため、そのまま解析できます |
| `.json` | 天鳳 JSON 形式（`tenhou.net/6`）のログ、または `--out-replay` で書き出した replay JSON |

牌譜の取得から解析までは実行ファイル内で完結します。**Python も外部ライブラリも不要**です。

### 雀魂の牌譜を用意する

雀魂は牌譜 URL からの自動ダウンロードを受け付けないため、ブラウザ経由で保存します。

1. [Tampermonkey](https://www.tampermonkey.net/) をブラウザに入れる
2. mjai-reviewer の [`downloadlogs` スクリプト](https://gist.githubusercontent.com/Equim-chan/875a232a2c1d31181df8b3a8704c3112/raw/a0533ae7a0ab0158ca9ad9771663e94b82b61572/downloadlogs.js) を追加する
3. 雀魂で検討したい牌譜を開き、読み込み後に <kbd>S</kbd> を押すとログが保存される
4. そのログを [mjai-reviewer](https://mjai.ekyu.moe/) にかけ、出力された HTML を保存する

天鳳の牌譜であれば、天鳳 JSON 形式のログをそのまま渡せます。

## ビルド

```bash
mkdir build && cd build
cmake .. -DBUILD_TOOLS=ON
cmake --build . --config Release
```

実行ファイルの隣に、向聴数テーブル (`suits_table.bin`, `honors_table.bin`) と
手牌分解パターン (`suits_patterns.json`, `honors_patterns.json`) がコピーされます。
実行時にこの 4 ファイルを**自分と同じフォルダ**から読み込むため、配布するときは
実行ファイルと合わせた 5 ファイルをフォルダごと配置してください。

### 配布用ビルド (Windows)

既定のビルドは MSVC ランタイム DLL に依存するため、Visual C++ 再頒布可能パッケージが
入っていない環境では起動できません。配布用には、CRT ごと静的リンクした実行ファイルを
作ります。`runtime-link=static` でビルドした Boost が必要です。

```bash
# Boost (filesystem, system) を静的 CRT でビルドしておく
b2 --with-filesystem --with-system variant=release link=static runtime-link=static ^
   address-model=64 --prefix=<boost-prefix> install

cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release ^
      -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DBoost_USE_STATIC_RUNTIME=ON ^
      -DCMAKE_PREFIX_PATH=<boost-prefix>/lib/cmake ^
      -DBUILD_SERVER=OFF -DBUILD_TEST=OFF -DBUILD_SAMPLES=OFF -DBUILD_TOOLS=ON
cmake --build build-release --target analyze_majsoul_paipu
```

こうすると依存は `KERNEL32.dll` と `SHELL32.dll` だけになり、追加のランタイムなしで
動作します。

## 使い方

### ドラッグ&ドロップ

牌譜ファイルを `analyze_majsoul_paipu.exe` にドロップすると、

- HTML レポートを**入力ファイルと同じ場所**に `<ファイル名>_report.html` として出力
- 解析後にレポートを既定のブラウザで開く（`--no-open` で抑止）
- コンソールはキー入力まで閉じない

シェルから実行した場合はこれらは働かず、指定したとおりに動作します。

### コマンドライン

```bash
# 解析して HTML レポートも出力する
analyze_majsoul_paipu 牌譜.html --html report.html

# 変換後の replay JSON を残す
analyze_majsoul_paipu 牌譜.html --out-replay replay.json

# 別の席を解析する
analyze_majsoul_paipu 牌譜.html --seat 2
```

解析対象の席は、レポートの検討対象（牌譜ビューアリンクの `tw=`）から自動判定します。

ビルドの動作確認には、同梱のサンプル牌譜が使えます。

```bash
analyze_majsoul_paipu data/testcase/majsoul_sample_replay.json
```

| オプション | 説明 |
| --- | --- |
| `--html <file>` | HTML レポートの出力先 |
| `--out-replay <file>` | 変換後の replay JSON を残す |
| `--seat <n>` | 解析対象の席を上書きする |
| `--max-shanten <n>` | この向聴数を超える局面をスキップ（既定 6 = 全局面を解析） |
| `--max-shanten-vs-riichi <n>` | 他家立直後は、この向聴数を超える局面をスキップ（既定 1）。6 で無効化 |
| `--extra <n>` | 期待値計算の基準となる探索範囲（既定 1、大きいほど正確で遅い） |
| `--wide-search-shanten <n>` | この向聴数以下で探索範囲を 1 広げる（既定 1、`-1` で無効） |
| `--narrow-search-shanten <n>` | この向聴数以上で探索範囲を 0 にする（既定 4） |
| `--include-riichi` | 自分の立直後のツモ切りも解析対象にする |
| `--simple-wall` | 河・副露を無視し、自分の手牌とドラ表示牌だけを山から除く |
| `--candidates <n>` | 1 局面あたりに表示する候補数（0 で全件、既定 6） |
| `--tile-style <style>` | 牌の表記。`unicode`（既定）または `mpsz`（`3m`/`0p`/`1z`） |
| `--no-open` | ドラッグ&ドロップ実行時にレポートを自動で開かない |
| `--quiet` | 進捗を表示しない |

## 出力

牌は既定で [Unicode の麻雀牌文字](https://mahjong-item.jp/unicode/)（U+1F000〜U+1F021）で表示します。

```
--- 東1局0本場 ---
[ 9巡目] 手牌 🀇🀇🀇🀈🀈🀌🀍🀎🀙🀟🀠🀘🀘🀘 ツモ 🀇
        副露 なし / ドラ表示 🀐 / 未確認 83枚 / 0向聴
        実打 🀙    期待値  2665.30  順位 1/9  損失    0.00  [立直宣言]
        最善 🀙    期待値  2665.30
          # 打牌     期待値    和了率    聴牌率 向聴 受入
          1 🀙       2665.30    61.31%   100.00%    0 2種7枚  <- 実打
          2 🀠       1629.23    29.60%    98.14%    1 9種24枚
          3 🀈       1623.67    38.72%    90.37%    1 5種15枚
```

最後にサマリーが出ます。

```
=== サマリー ===
打牌数          : 98
解析した局面    : 78
除外 (立直中)   : 20
除外 (向聴上限) : 0
除外 (他家立直中) : 0
最善打牌一致    : 49 / 78 (62.8%)
平均順位        : 1.73
平均期待値損失  : 49.09
合計期待値損失  : 3829.02
```

HTML レポートには同じ内容に加えて、損失の大きい局面の一覧と、局面ごとの全候補表が入ります。
一覧の行をクリックすると、下部にあるその局面の詳細へジャンプします。折りたたまれていれば
開いた状態で表示し、飛んだ先を一瞬ハイライトします。

### 麻雀牌フォントについて

牌の文字は Unicode の Mahjong Tiles ブロックにあり、表示にはこのブロックを持つフォントが必要です。

- **Windows** … `Segoe UI Symbol`（標準搭載）。コンソールは Windows Terminal を推奨します
- **macOS** … `Apple Symbols`（標準搭載）
- **Linux** … `Noto Sans Symbols 2` または `Symbola` の導入が必要な場合があります

HTML レポートはこれらを並べたフォントスタックを指定しています。コンソールで豆腐（□）に
なる場合は `--tile-style mpsz` で `3m` 表記に切り替えてください。

- **赤ドラ** … Unicode に赤 5 の文字はありません。コンソールでは `🀋赤` のように「赤」を
  付け、HTML レポートでは牌を赤色で表示します（マウスオーバーで `0m` 等を表示）
- **中 (`7z`)** … `U+1F004` は既定が絵文字表示のため、異体字セレクタ `U+FE0E` を付けて
  他の牌と同じ文字表示に揃えています
- コンソールでは牌グリフの表示幅が環境によって変わるため、牌の列が多少ずれることがあります。
  数値の列は揃います

## 解析の前提

この解析が何を測っていて何を測っていないかは、結果を読むうえで重要です。

- **期待値の定義** … 本家の `ExpectedScoreCalculator` の値をそのまま使っています。和了時の得点に
  和了確率を掛けた**局単位の得点期待値**であり、放銃・被ツモの失点や順位期待値は含みません。
  したがって「押し引き」の判断ではなく、**和了に向けた牌効率と打点のバランス**の指標です。
- **山の推定** … その局面で自分から見えていない牌（全員の河・副露牌・ドラ表示牌・自分の
  手牌を除いた残り）を山とみなします。`--simple-wall` を付けると、何切るシミュレーターと
  同じ「自分の手牌とドラ表示牌のみ除外」に切り替わります。
- **巡目** … 自分の打牌数 + 1 を巡目としています（副露で巡目がずれる分は近似）。
- **自分の立直後** … 打牌の選択余地がないため既定では解析対象外です。立直宣言牌そのものは
  自由選択なので解析されます。
- **他家の立直後** … 2 向聴以上の局面は既定で解析対象外です。この状況では牌効率ではなく
  ベタ降りを選ぶのが普通で、和了に向けた期待値は判断基準にならないためです。
  閾値は `--max-shanten-vs-riichi` で変更できます。
- **フリテン** … 考慮していません。ただしこの計算モデルは和了をすべて**ツモ和了**として
  扱っており（`win_flag` は常に `WinFlag::Tsumo`）、ロンを一切モデル化していないため、
  ロンを禁じるフリテンは数値に影響しません。裏を返すと、**フリテンの待ちと非フリテンの
  待ちが同じ評価**になり、出和了りのしやすさも反映されません。
- **裏ドラ** … 実際にめくられた裏ドラではなく、確率的な期待値として計算に含まれます。
- **探索範囲の自動調整** … 期待値計算は現在の向聴数から `--extra` 分だけ悪い手も探索
  します。この探索は向聴数が増えるほど急激に重くなる一方、価値はテンパイに近いほど
  高いため、向聴数に応じて範囲を変えています。

  | 向聴 | 探索範囲 | 理由 |
  | --- | --- | --- |
  | 1 以下 | `--extra` + 1 | 僅差になりやすく、広げる価値が高い |
  | 2〜3 | `--extra` | 標準 |
  | 4 以上 | 0 | 判断が問われない局面に時間を使わない |

  該当した局面数はサマリーに「探索を拡大」「探索を縮小」として表示されます。
  閾値は `--wide-search-shanten` と `--narrow-search-shanten` で変更できます。

  実測（半荘 1 回、48 局面）では、全局面を `--extra 1` で解析すると 56 秒かかり、その
  うち 52 秒を 4 向聴の 5 局面だけが消費していました。上記の配分にすると 13 秒で終わり、
  かつ 1 向聴以下の 24 局面はより広く探索されます。

  なお探索を縮小した局面では向聴数が悪化する打牌が候補に含まれないため、実際の打牌が
  それに当たると「候補に見つかりません」となり集計から外れます。

  精度を上げたい場合は `--wide-search-shanten 2` や `--extra 2` を指定できますが、
  効果は逓減します。1 向聴以下で探索範囲を 2 から 3 に広げた計測では、実行時間が 21 倍に
  なる一方、最善打牌が変わったのは 24 局面中 1 件、期待値の変化は平均 1.7% でした。
- **処理時間** … 半荘 1 回で十数秒〜1 分程度です。急ぐときは `--wide-search-shanten -1`
  で拡大を止めると数秒で終わります。

## 構成

このフォークで追加したファイルは `src/tools/majsoul/` 以下です。

| ファイル | 役割 |
| --- | --- |
| `analyze_majsoul_paipu.cpp` | CLI。入力の判別、ドラッグ&ドロップ対応 |
| `tenhou_log.cpp` | 保存した牌譜（HTML / 天鳳 JSON）→ replay JSON |
| `replay_json.cpp` | replay JSON → `mahjong::RoundRecord` |
| `discard_analyzer.cpp` | 局を再生し、各打牌局面を `ExpectedScoreCalculator` にかける |
| `report.cpp` | コンソール表と HTML レポート |
| `tile_glyph.cpp` | 牌 → 麻雀牌文字 (U+1F000〜) の変換 |

本家のライブラリには 1 点だけ変更を加えています。`src/mahjong/types/round_event.hpp` の
`RoundEvent` に、三人麻雀の北抜きを表す `NukiEvent` を追加しました。これがないと三麻の
牌譜で手牌枚数が合わなくなるためです。既存の利用箇所はすべて `std::get_if` を使っており、
選択肢の追加による影響はありません。

## replay JSON (`majsoul-replay/1`)

`tenhou_log.cpp` が出力し、`replay_json.cpp` が読む中間形式です。`--out-replay` で
書き出せます。この形式を自分で用意すれば、任意の牌譜を解析できます。

牌は mpsz 表記の文字列（`"3p"`、`"1z"`、赤 5 は `"0m"`）です。

```jsonc
{
  "schema": "majsoul-replay/1",
  "uuid": "",
  "url": "",
  "game_mode": "yonma",          // または "sanma"
  "game_length": "hanchan",      // または "tonpu"
  "rules": { "red_dora": true, "ura_dora": true, "open_tanyao": true },
  "target_seat": 0,
  "players": [ { "seat": 0, "name": "...", "level": "..." }, ... ],
  "rounds": [
    {
      "round_wind": 0,           // 0=東, 1=南, 2=西
      "round_number": 1,
      "honba": 0,
      "kyotaku": 0,
      "dealer": 0,
      "scores": [25000, 25000, 25000, 25000],
      "hands": [ ["1m", ...13枚], ... ],
      "dora_indicators": ["3p"],
      "events": [
        { "type": "draw",     "actor": 0, "tile": "5s" },
        { "type": "discard",  "actor": 0, "tile": "1z", "tsumogiri": false },
        { "type": "riichi",   "actor": 0 },
        { "type": "call",     "actor": 1, "meld_type": "pon",
          "tiles": ["2p","2p","2p"], "called_tile": "2p", "from": 0 },
        { "type": "nuki",     "actor": 0 },
        { "type": "dora",     "tile": "7p" },
        { "type": "tsumo",    "winner": 0, "tile": "3m" },
        { "type": "ron",      "winner": 0, "loser": 2, "tile": "3m" },
        { "type": "ryukyoku", "reason": "exhaustive" }
      ]
    }
  ]
}
```

- `hands` は必ず 13 枚です。親の 14 枚目は最初の `draw` イベントとして出します。
- `dora_indicators` には開局時の 1 枚だけを入れます。カンドラは槓の直後に `dora`
  イベントとして出すため、その局面で見えていた枚数だけが計算に入ります。
- `riichi` イベントは**宣言牌の打牌の直後**に置きます。宣言牌自体は自由選択として解析され、
  それ以降のツモ切りが「立直中」として扱われます。
- `meld_type` は `chi` / `pon` / `ankan` / `daiminkan` / `kakan`。`ankan` は
  `called_tile` を持ちません。`kakan` の `tiles` は 4 枚、`called_tile` は加えた牌です。
- `from` は絶対席番号です（読み込み時に相対席へ変換します）。
- 和了・流局はイベントとして持ちますが、点数の増減や役の内訳は保持していません。
  解析に不要なためです。

## 制限事項

- 槓のうち**加槓は実データで動作を確認済み**です（副露表示とカンドラのめくりを含む）。
  暗槓と大明槓は実装してありますが、検証に使った牌譜に出現しなかったため未確認です。
- 三人麻雀は実装してありますが、四人麻雀ほど検証できていません。
- 牌譜の入手経路は本ツールの範囲外です。雀魂は自動ログインを受け付けないため、
  上記のとおりブラウザ経由で保存したファイルを渡してください。
