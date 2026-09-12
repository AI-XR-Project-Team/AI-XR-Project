param(
    [string]$Source,
    [string]$OutDir,
    [int]$SegThresh  = 170,
    [int]$TrimThresh = 10,
    [int]$MinArea    = 500,
    [int]$MergeGap   = 10
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Drawing

$code = @"
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class SheetSlicer
{
    public class Box { public int X0, Y0, X1, Y1, Area; }

    public static List<string> Slice(string src, string outDir, int segT, int trimT, int minArea, int mergeGap)
    {
        var log = new List<string>();
        Bitmap bmp = new Bitmap(src);
        int W = bmp.Width, H = bmp.Height;
        var rect = new Rectangle(0, 0, W, H);
        var data = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
        int stride = data.Stride;
        byte[] buf = new byte[stride * H];
        Marshal.Copy(data.Scan0, buf, 0, buf.Length);
        bmp.UnlockBits(data);

        byte[] a = new byte[W * H];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                a[y * W + x] = buf[y * stride + x * 4 + 3];

        // connected components on alpha >= segT (8-neighbour BFS)
        int[] label = new int[W * H];
        var boxes = new List<Box>();
        var stack = new Stack<int>();
        int cur = 0;
        int[] dx = { -1, 0, 1, -1, 1, -1, 0, 1 };
        int[] dy = { -1, -1, -1, 0, 0, 1, 1, 1 };
        for (int s = 0; s < a.Length; s++)
        {
            if (a[s] < segT || label[s] != 0) continue;
            cur++;
            stack.Push(s); label[s] = cur;
            int x0 = W, x1 = -1, y0 = H, y1 = -1, cnt = 0;
            while (stack.Count > 0)
            {
                int p = stack.Pop(); cnt++;
                int px = p % W, py = p / W;
                if (px < x0) x0 = px; if (px > x1) x1 = px;
                if (py < y0) y0 = py; if (py > y1) y1 = py;
                for (int k = 0; k < 8; k++)
                {
                    int nx = px + dx[k], ny = py + dy[k];
                    if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                    int np = ny * W + nx;
                    if (a[np] >= segT && label[np] == 0) { label[np] = cur; stack.Push(np); }
                }
            }
            if (cnt >= minArea) boxes.Add(new Box { X0 = x0, Y0 = y0, X1 = x1, Y1 = y1, Area = cnt });
        }
        log.Add("raw components: " + boxes.Count);

        // merge boxes that overlap or nearly touch (glow bits belong to one sprite)
        bool merged = true;
        while (merged)
        {
            merged = false;
            for (int i = 0; i < boxes.Count && !merged; i++)
                for (int j = i + 1; j < boxes.Count && !merged; j++)
                {
                    var A = boxes[i]; var B = boxes[j];
                    bool near = A.X0 - mergeGap <= B.X1 && B.X0 - mergeGap <= A.X1
                             && A.Y0 - mergeGap <= B.Y1 && B.Y0 - mergeGap <= A.Y1;
                    if (!near) continue;
                    A.X0 = Math.Min(A.X0, B.X0); A.Y0 = Math.Min(A.Y0, B.Y0);
                    A.X1 = Math.Max(A.X1, B.X1); A.Y1 = Math.Max(A.Y1, B.Y1);
                    A.Area += B.Area;
                    boxes.RemoveAt(j);
                    merged = true;
                }
        }
        log.Add("after merge: " + boxes.Count);

        boxes.Sort(delegate (Box p, Box q) {
            int r = (p.Y0 / 40).CompareTo(q.Y0 / 40);
            return r != 0 ? r : p.X0.CompareTo(q.X0);
        });

        System.IO.Directory.CreateDirectory(outDir);
        int idx = 0;
        foreach (var b in boxes)
        {
            // Pad the component box for glow. Do NOT re-trim at a low alpha
            // threshold: the sheet prints a faint caption under every sprite and
            // a low threshold drags that text into the crop.
            int tx0 = Math.Max(0, b.X0 - trimT);
            int tx1 = Math.Min(W - 1, b.X1 + trimT);
            int ty0 = Math.Max(0, b.Y0 - trimT);
            int ty1 = Math.Min(H - 1, b.Y1 + trimT);
            if (tx1 < tx0 || ty1 < ty0) continue;
            int w = tx1 - tx0 + 1, h = ty1 - ty0 + 1;
            idx++;
            string name = string.Format("{0:D3}_{1}x{2}.png", idx, w, h);
            using (var crop = new Bitmap(w, h))
            {
                using (var g = Graphics.FromImage(crop))
                    g.DrawImage(bmp, new Rectangle(0, 0, w, h), new Rectangle(tx0, ty0, w, h), GraphicsUnit.Pixel);
                crop.Save(System.IO.Path.Combine(outDir, name), ImageFormat.Png);
            }
            log.Add(string.Format("{0}\t{1}\tx={2} y={3} w={4} h={5}", idx, name, tx0, ty0, w, h));
        }
        bmp.Dispose();
        log.Add("saved: " + idx);
        return log;
    }
}
"@

Add-Type -TypeDefinition $code -ReferencedAssemblies System.Drawing
$result = [SheetSlicer]::Slice($Source, $OutDir, $SegThresh, $TrimThresh, $MinArea, $MergeGap)
$result | ForEach-Object { Write-Host $_ }
