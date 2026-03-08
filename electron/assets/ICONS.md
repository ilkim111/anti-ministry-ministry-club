## App Icons

electron-builder needs platform-specific icons:

- **macOS**: `icon.icns` (1024x1024)
- **Windows**: `icon.ico` (256x256)
- **Linux**: `icons/` directory with PNGs (16x16 through 512x512)

### Generate from SVG

```bash
# Install dependencies
npm install -g icon-gen

# Or use ImageMagick
convert -background none icon.svg -resize 512x512 icon.png
convert icon.png -resize 256x256 icon.ico

# For macOS .icns (on macOS)
mkdir icon.iconset
for size in 16 32 64 128 256 512; do
  convert -background none icon.svg -resize ${size}x${size} icon.iconset/icon_${size}x${size}.png
  convert -background none icon.svg -resize $((size*2))x$((size*2)) icon.iconset/icon_${size}x${size}@2x.png
done
iconutil -c icns icon.iconset

# For Linux
for size in 16 32 48 64 128 256 512; do
  convert -background none icon.svg -resize ${size}x${size} icons/${size}x${size}.png
done
```

electron-builder can also auto-convert from a single 512x512+ PNG named `icon.png`.
Place a `icon.png` (512x512 or larger) in this directory and it will handle the rest.
