# Lumo Dynamic Theme Engine

## Overview

The theme engine adapts all UI colors based on time of day and weather
conditions. Colors are computed using continuous interpolation between
key-color stops placed at specific hours, with smoothstep easing for
natural transitions. Weather shifts are blended in using exponential
approach (8% per update) so condition changes fade in gradually rather
than popping.

## Time-of-Day Color Stops

The engine defines 12 key-color stops across the 24-hour cycle. Between
any two adjacent stops, colors are interpolated using smoothstep
(`t*t*(3-2t)`) for a natural curve. This replaces the earlier discrete
7-period system with a continuous gradient.

| Stop | Hour | Palette | Base RGB |
|------|------|---------|----------|
| Midnight | 0:00 | Deep aubergine | 0x12, 0x08, 0x1A |
| Pre-dawn | 4:00 | Slight blue lift | 0x10, 0x0C, 0x20 |
| Dawn | 5:30 | Cool teal | 0x14, 0x28, 0x38 |
| Early morning | 7:00 | Warm rose | 0x30, 0x10, 0x28 |
| Mid-morning | 10:00 | Pure aubergine | 0x2C, 0x00, 0x1E |
| Midday | 13:00 | Hold aubergine | 0x2C, 0x00, 0x1E |
| Afternoon | 15:00 | Dusty warm | 0x28, 0x14, 0x18 |
| Late afternoon | 17:00 | Sunset orange | 0x42, 0x0C, 0x16 |
| Dusk | 19:00 | Purple | 0x30, 0x0A, 0x22 |
| Twilight | 20:30 | Deep blue | 0x10, 0x18, 0x30 |
| Night | 22:00 | Aubergine | 0x12, 0x08, 0x1A |
| Wrap | 24:00 | (= midnight) | 0x12, 0x08, 0x1A |

## Smooth Blending

The engine maintains a running smooth color state (`lumo_smooth_r/g/b`).
Each update computes the target color from time-of-day interpolation plus
weather modification, then moves the current color 8% of the way toward
the target (exponential approach). This prevents jarring color jumps when
the weather condition changes or the time crosses a stop boundary.

## Weather Modifications

Weather conditions shift the interpolated base RGB values:

| Code | Condition | Effect |
|------|-----------|--------|
| 0 | Clear/Sunny | No shift (warm base) |
| 1 | Partly Cloudy | +blue push |
| 2 | Cloudy | Grey-slate blend |
| 3 | Rain | Deep teal-blue |
| 4 | Storm | Purple-indigo |
| 5 | Snow | Ice blue-white lift |
| 6 | Fog | Warm grey wash |

## Derived Colors

From the base RGB, the engine derives:

- `bar_top/bar_bottom`: status bar gradient (base + tint)
- `panel_bg`: panel/drawer background (slightly lighter)
- `panel_stroke`: borders (base + 0x30/0x18/0x28)
- `tile_fill/tile_stroke`: drawer tiles
- `accent`: always Ubuntu orange `#E95420`
- `text_primary`: always white
- `text_secondary`: always warm grey `#AEA79F`
- `dim/separator`: translucent version of stroke color

## Animated Background (PS4 Flow Waves)

The background renders 7 sine-based wave curves with asymmetric glow falloff,
composited onto the time/weather gradient:

- **Rendering**: Real-time wave computation (no pre-rendered buffer)
- **Resolution**: Half-res glow buffer (400×640 uint8), upscaled 2× for composite
- **GPU compositing**: PowerVR BXE-2-32 handles final composite
- **CPU**: ~15% for wave computation (8 threads at 15fps)
- **Boot splash**: Lumo icon + Ubuntu-style three-dot indicator

The gradient background smoothly interpolates between hour palettes based on
the current minute, so there are no hard color jumps at hour boundaries.

## WiFi Signal

Status bar wifi bars update every 10 seconds using `iw dev wlan0 link` to read
signal strength in dBm. Fallback to `/proc/net/wireless` if available.

| Signal     | Bars           |
| ---------- | -------------- |
| > -50 dBm  | 4 (excellent)  |
| > -60 dBm  | 3 (good)       |
| > -70 dBm  | 2 (fair)       |
| > -90 dBm  | 1 (weak)       |

## Weather Data Source

Weather is fetched every 5 minutes from `wttr.in/41101` using:
```
curl -s --max-time 8 'https://wttr.in/41101?format=%t+%C+%h+%w&u'
```

Returns: temperature (F), condition, humidity (%), wind speed.
