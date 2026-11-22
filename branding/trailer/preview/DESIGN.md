# 实机样片字卡

## Style Prompt

黑屏中听上弹，枪声起音时整枚会所片名同时出现。实机镜头只在一处玩法初次出现时配一枚短签。字签贴近上沿，旁白字幕在画面下方居中，给马、人物、游戏对白和两侧提示留出空间。

## Colors

- 纸白 `#eee9dc` 用于主文字和马蹄徽记。
- 暖灰 `#e9e6df` 用于说明文字。
- 深墨 `#101010` 用于局部字底。
- 印红 `#c52027` 用于片名徽记两侧的细线。

## Typography

片名使用会所已有的 RDR Lino，沿用会所马蹄轮廓。中文字签用宋体，英文小标使用 Hapna Slab Serif。版面以 2580 × 1080 制作，小字仍以横屏手机可辨为尺度。字体和墨刷轮廓来自会所的设计规范。

## What NOT to Do

原声招呼、赔率、赛果及人物名字展示期间给游戏界面留出完整阅读时间。片名直接击入，配合枪声落定。字签的淡入淡出由主时间线控制。

## 本机重新导出

保留「视频 / Red Dead Redemption 2」中的原片、`粗剪` 目录（含片头枪声 WAV），以及 `宣传片样片/工程素材` 内的 `英文旁白.wav` 和 `速度对比.mp4`。镜头和字签的时间写在 `index.html`，双语台词及起止时间写在 `narration.json`。英文旁白音轨已对齐整片五分钟，采样率为 48 kHz。

在本目录打开 PowerShell。以下命令使用本机已配好的 Node.js、Playwright、Chrome、带 Pillow 的 Python，以及支持 H.264 NVENC 的剪映 FFmpeg。每次改剪都用一个新的临时目录，以便按最新切点出片。

```powershell
$mediaDir = Join-Path ([Environment]::GetFolderPath('MyVideos')) 'Red Dead Redemption 2'
$workDir = Join-Path $mediaDir ('宣传片样片/.render-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
$trailerPython = (Get-Command python).Source
$trailerFfmpeg = (Get-Command ffmpeg).Source

node .\export-graphics.cjs $workDir
& $trailerPython .\render.py shots    --media $mediaDir --work $workDir --ffmpeg $trailerFfmpeg
& $trailerPython .\render.py assemble --media $mediaDir --work $workDir --ffmpeg $trailerFfmpeg
& $trailerPython .\render.py compose  --media $mediaDir --work $workDir --ffmpeg $trailerFfmpeg
```

先导出字卡和字幕的透明图层，再裁镜头、拼时间线、叠字混音。MP4 与同名双语 SRT 写入 `宣传片样片`。重新导出前，把需要保留的旧版 MP4 和 SRT 改名归档。
