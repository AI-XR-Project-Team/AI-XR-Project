<#
.SYNOPSIS
    마스터 PNG 한 장에서 안드로이드 적응형 아이콘 전경 리소스를 생성한다.

.DESCRIPTION
    적응형 아이콘 전경은 108dp 캔버스이고 바깥 18dp 는 런처 마스크에 잘린다.
    그림은 가운데 72dp(66.7%) 안에 있어야 안전하다. 이 스크립트가 그 여백을
    만들어 주므로, 마스터는 그림이 꽉 찬 정사각 PNG 한 장이면 된다.

    해상도별로 GPT 에 여러 장 요구하지 않는 이유는, 리사이즈는 결정적인
    작업이라 여기서 하는 편이 틀릴 여지가 없기 때문이다.

.PARAMETER Source
    마스터 PNG 경로. 정사각형이어야 하고 512px 이상을 권장한다.

.PARAMETER Inset
    캔버스 대비 그림이 차지할 비율. 기본 0.66.
    마스터가 이미 안전영역을 지켜 여백을 갖고 있다면 1.0 을 준다.

.EXAMPLE
    pwsh tools/generate_android_icons.ps1 -Source docs/ai-handoff/inbox/icon_foreground.png
#>
param(
    [Parameter(Mandatory = $true)][string]$Source,
    [double]$Inset = 0.66
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

# 108dp 기준 밀도별 픽셀 크기
$Targets = [ordered]@{
    "mipmap-mdpi"    = 108
    "mipmap-hdpi"    = 162
    "mipmap-xhdpi"   = 216
    "mipmap-xxhdpi"  = 324
    "mipmap-xxxhdpi" = 432
}

$RepoRoot = Split-Path -Parent $PSScriptRoot
$ResRoot  = Join-Path $RepoRoot "TimeMachineAR\Build\Android\res"

if (-not (Test-Path $Source)) { throw "마스터 이미지를 찾을 수 없다: $Source" }
$src = [System.Drawing.Image]::FromFile((Resolve-Path $Source))
try {
    Write-Host ("원본: {0}x{1}  {2}" -f $src.Width, $src.Height, $src.PixelFormat)
    if ($src.Width -ne $src.Height) {
        throw "정사각형이 아니다 ($($src.Width)x$($src.Height)). 런처가 찌그러뜨린다."
    }
    if ($src.Width -lt 432) {
        Write-Warning "원본이 $($src.Width)px 라 xxxhdpi(432px) 에서 흐려진다. 1024px 를 권장한다."
    }

    foreach ($name in $Targets.Keys) {
        $size = $Targets[$name]
        $dir = Join-Path $ResRoot $name
        New-Item -ItemType Directory -Force -Path $dir | Out-Null

        $bmp = New-Object System.Drawing.Bitmap $size, $size
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        try {
            $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
            $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $g.Clear([System.Drawing.Color]::Transparent)

            # 안전영역 안에 가운데 정렬
            $draw = [int][Math]::Round($size * $Inset)
            $off  = [int][Math]::Round(($size - $draw) / 2.0)
            $g.DrawImage($src, $off, $off, $draw, $draw)
        }
        finally { $g.Dispose() }

        $out = Join-Path $dir "ic_launcher_foreground.png"
        $bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host ("  {0,-16} {1,3}px  (그림 {2}px)  ->  {3}" -f $name, $size, $draw, $out)
    }
}
finally { $src.Dispose() }

Write-Host "`n완료. 패키징하면 런처 아이콘에 반영된다."
