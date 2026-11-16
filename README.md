# Fallout Community Edition 日本語化（nettoge.com の改造版）

**This software has been modified by nettoge.com.** This is a fork of [Fallout Community Edition](https://github.com/alexbatalov/fallout1-ce) by alexbatalov that draws Japanese text. It is not made or endorsed by the original author, Bethesda Softworks or Interplay.

初代 Fallout（Steam 版）を日本語で遊ぶための、fallout1-ce の改造版です。日本語の文・フォント・画面の絵は [Releases](https://github.com/mattarilightcha/fallout1-ce/releases) の zip に入っています。入れ方と解説は [nettoge.com](https://nettoge.com/fallout1-japanese/) の記事にあります。

## 入れ方

1. [Releases](https://github.com/mattarilightcha/fallout1-ce/releases) から `fallout1-japanese-1.01.zip` を落とし、中身を全部 Steam の Fallout のフォルダへ写します（Steam のライブラリで Fallout を右クリック →「管理」→「ローカルファイルを閲覧」）。`fallout-ce.exe` と `DATA` が `FALLOUTW.EXE`・`MASTER.DAT` と同じ所に並べば合っています。Steam のファイルは上書きしません。
2. 同じフォルダの `fallout.cfg` で、`language=english` を `language=japanese` に、`subtitles=0` を `subtitles=1` に書き換えます。
3. `fallout-ce.exe` を起動します。起動しない時は Microsoft Visual C++ 再頒布可能パッケージ（x86）を入れてください。

## 改造した所

元のコードからの変更には、どれも `nettoge.com:` の注記を付けています。

- 日本語の文字の描画（UTF-8、ビットマップのフォント `.jfn`、禁則つきの折り返し）: `src/plib/gnw/jtext.cc`・`jtext.h`、`text.cc`・`wordwrap.cc`・`int/window.cc`
- 会話・キャラクター作成・持ち物・セーブとロード・設定・キャラクター選択の、日本語の字に合わせた行の高さと位置: `gdialog.cc`・`editor.cc`・`inventry.cc`・`loadsave.cc`・`options.cc`・`select.cc`
- Pip-Boy の紙の日数を、手書きの日本語と同じ字で「日」の横に水平に並べる（数字の絵 `NOTENUMS.FRM` は描き直した物を zip に入れています）: `pipboy.cc`
- 主人公の性別で会話の言い方を切り替える `<男の文^女の文>`: `message.cc`
- 映像の崩れの直し: `movie_lib.cc` の `getOffset` が行の位置を `uint8_t` で返していて、大きな映像のブロックが別の行へずれていました（元の fallout1-ce にもある不具合です）
- 開発・撮影ビルド（`-DNTG_SHOOT=ON`、`src/ntg_shoot.cc`）。画面を外から操作して撮るための物で、配る `fallout-ce.exe` には入っていません

## 組み方（Windows・Visual Studio 2022・Win32）

```
cmake -S . -B build -G "Visual Studio 17 2022" -A Win32 -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build build --config RelWithDebInfo
```

## 使用許諾

元の fallout1-ce と同じ Sustainable Use License 1.0 です（[LICENSE.md](LICENSE.md)、変えていません）。配れるのは、無料で、非営利の目的の時だけです。日本語のフォントは SIL Open Font License 1.1 のフォントから作り、全文を Releases の zip の `ライセンス` フォルダに入れています。Fallout は Bethesda Softworks LLC の商標です。

---

以下は元の fallout1-ce の README です。

# Fallout Community Edition

Fallout Community Edition is a fully working re-implementation of Fallout, with the same original gameplay, engine bugfixes, and some quality of life improvements, that works (mostly) hassle-free on multiple platforms.

There is also [Fallout 2 Community Edition](https://github.com/alexbatalov/fallout2-ce).

## Installation

You must own the game to play. Purchase your copy on [GOG](https://www.gog.com/game/fallout) or [Steam](https://store.steampowered.com/app/38400). Download latest [release](https://github.com/alexbatalov/fallout1-ce/releases) or build from source. You can also check latest [debug](https://github.com/alexbatalov/fallout1-ce/actions) build intended for testers.

### Windows

Download and copy `fallout-ce.exe` to your `Fallout` folder. It serves as a drop-in replacement for `falloutw.exe`.

### Linux

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout` folder somewhere, for example `/home/john/Desktop/Fallout`.

- Alternatively you can extract the needed files from the GoG installer:

```console
$ sudo apt install innoextract
$ innoextract ~/Downloads/setup_fallout_2.1.0.18.exe -I app
$ mv app Fallout
```

- Download and copy `fallout-ce` to this folder.

- Install [SDL2](https://libsdl.org/download-2.0.php):

```console
$ sudo apt install libsdl2-2.0-0
```

- Run `./fallout-ce`.

### macOS

> **NOTE**: macOS 10.11 (El Capitan) or higher is required. Runs natively on Intel-based Macs and Apple Silicon.

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout` folder somewhere, for example `/Applications/Fallout`.

- Alternatively you can use Fallout from MacPlay/The Omni Group as a base - you need to extract game assets from the original bundle. Mount CD/DMG, right click `Fallout` -> `Show Package Contents`, navigate to `Contents/Resources`. Copy `GameData` folder somewhere, for example `/Applications/Fallout`.

- Or if you're a Terminal user and have Homebrew installed you can extract the needed files from the GoG installer:

```console
$ brew install innoextract
$ innoextract ~/Downloads/setup_fallout_2.1.0.18.exe -I app
$ mv app /Applications/Fallout
```

- Download and copy `fallout-ce.app` to this folder.

- Run `fallout-ce.app`.

### Android

> **NOTE**: Fallout was designed with mouse in mind. There are many controls that require precise cursor positioning, which is not possible with fingers. Current control scheme resembles trackpad usage:
> - One finger moves mouse cursor around.
> - Tap one finger for left mouse click.
> - Tap two fingers for right mouse click (switches mouse cursor mode).
> - Move two fingers to scroll current view (map view, worldmap view, inventory scrollers).

> **NOTE**: From Android standpoint release and debug builds are different apps. Both apps require their own copy of game assets and have their own savegames. This is intentional. As a gamer just stick with release version and check for updates.

- Use Windows installation as a base - it contains data assets needed to play. Copy `Fallout` folder to your device, for example to `Downloads`. You need `master.dat`, `critter.dat`, and `data` folder. Watch for file names - keep (or make) them lowercased (see [Configuration](#configuration)).

- Download `fallout-ce.apk` and copy it to your device. Open it with file explorer, follow instructions (install from unknown source).

- When you run the game for the first time it will immediately present file picker. Select the folder from the first step. Wait until this data is copied. A loading dialog will appear, just wait for about 30 seconds. The game will start automatically.

### iOS

> **NOTE**: See Android note on controls.

- Download `fallout-ce.ipa`. Use sideloading applications ([AltStore](https://altstore.io/) or [Sideloadly](https://sideloadly.io/)) to install it to your device. Alternatively you can always build from source with your own signing certificate.

- Run the game once. You'll see error message saying "Could not find the master datafile...". This step is needed for iOS to expose the game via File Sharing feature.

- Use Finder (macOS Catalina and later) or iTunes (Windows and macOS Mojave or earlier) to copy `master.dat`, `critter.dat`, and `data` folder to "Fallout" app ([how-to](https://support.apple.com/HT210598)). Watch for file names - keep (or make) them lowercased (see [Configuration](#configuration)).

## Configuration

The main configuration file is `fallout.cfg`. There are several important settings you might need to adjust for your installation. Depending on your Fallout distribution main game assets `master.dat`, `critter.dat`, and `data` folder might be either all lowercased, or all uppercased. You can either update `master_dat`, `critter_dat`, `master_patches` and `critter_patches` settings to match your file names, or rename files to match entries in your `fallout.cfg`.

The `sound` folder (with `music` folder inside) might be located either in `data` folder, or be in the Fallout folder. Update `music_path1` setting to match your hierarchy, usually it's `data/sound/music/` or `sound/music/`. Make sure it match your path exactly (so it might be `SOUND/MUSIC/` if you've installed Fallout from CD). Music files themselves (with `ACM` extension) should be all uppercased, regardless of `sound` and `music` folders.

The second configuration file is `f1_res.ini`. Use it to change game window size and enable/disable fullscreen mode.

```ini
[MAIN]
SCR_WIDTH=1280
SCR_HEIGHT=720
WINDOWED=1
```

Recommendations:
- **Desktops**: Use any size you see fit.
- **Tablets**: Set these values to logical resolution of your device, for example iPad Pro 11 is 1668x2388 (pixels), but it's logical resolution is 834x1194 (points).
- **Mobile phones**: Set height to 480, calculate width according to your device screen (aspect) ratio, for example Samsung S21 is 20:9 device, so the width should be 480 * 20 / 9 = 1067.

In time this stuff will receive in-game interface, right now you have to do it manually.

## Contributing

Here is a couple of current goals. Open up an issue if you have suggestion or feature request.

- **Update to v1.2**. This project is based on Reference Edition which implements v1.1 released in November 1997. There is a newer v1.2 released in March 1998 which at least contains important multilingual support.

- **Backport some Fallout 2 features**. Fallout 2 (with some Sfall additions) added many great improvements and quality of life enhancements to the original Fallout engine. Many deserve to be backported to Fallout 1. Keep in mind this is a different game, with slightly different gameplay balance (which is a fragile thing on its own).

## License

The source code is this repository is available under the [Sustainable Use License](LICENSE.md).
