# Stretch - After Effects エフェクトプラグイン

After Effects用のエフェクトプラグインです。アンカーポイントと角度に基づいてピクセルをストレッチします。

## 概要

`Stretch`は、指定したアンカーポイントと角度に基づいて画像をストレッチするエフェクトプラグインです。ピクセルを指定方向にシフトさせ、視覚的なストレッチ効果を生成します。

## 機能

- **アンカーポイント**: ストレッチの基準点を設定
- **角度**: ストレッチの方向を角度で指定
- **シフト量**: ストレッチの強度を調整（0-10000）
- **方向**: ストレッチの方向を選択（両方向/前方/後方）

## パラメータ

1. **Anchor Point** (アンカーポイント)
   - ストレッチの基準となる座標点

2. **Angle** (角度)
   - ストレッチの方向を角度で指定

3. **Shift Amount** (シフト量)
   - ストレッチの強度（0-10000）

4. **Direction** (方向)
   - Both: 両方向にストレッチ
   - Forward: 前方のみストレッチ
   - Backward: 後方のみストレッチ

## ビルド

### Windows

Visual Studio 2022以降を使用してビルドします。

1. `Win/Stretch.sln`を開く
2. プラットフォームを選択（x64 または ARM64）
3. ビルド構成を選択（Debug または Release）
4. ビルドを実行

出力ファイル: `Stretch.aex`

### macOS

Xcodeを使用してビルドします。

1. `Mac/Stretch.xcodeproj`を開く
2. スキームを選択（Debug または Release）
3. ビルドを実行

出力ファイル: `Stretch.plugin`

## システム要件

- After Effects CC以降
- Windows: x64 または ARM64
- macOS: Intel64 または ARM64

## バージョン情報

- バージョン: 1.2.0
- 開発段階: Development

## サポート

- サポートURL: https://x.com/361do_sleep
- カテゴリ: 361do_plugins

## ライセンス

このプロジェクトはAdobe After Effects SDKテンプレートに基づいています。
