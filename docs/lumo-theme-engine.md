# Lumo Dynamic Theme Engine

## Overview

The theme engine adapts all UI colors based on time of day and weather
conditions. Colors are computed once per minute (on hour change) or when
weather data arrives.

## Time-of-Day Palettes

The base RGB values shift through 7 periods inspired by three mobile OS
palettes:

| Period | Hours | Palette | Base RGB |
|--------|-------|---------|----------|
| Dawn | 5-7am | Sailfish teal | 0x14, 0x28, 0x38 |
| Morning | 7-10am | Ubuntu + Sailfish | 0x30, 0x10, 0x28 |
| Midday | 10am-2pm | Ubuntu aubergine | 0x2C, 0x00, 0x1E |
| Afternoon | 2-5pm | webOS charcoal | 0x28, 0x14, 0x18 |
| Sunset | 5-7pm | Ubuntu orange-red | 0x42, 0x0C, 0x16 |
| Evening | 7-9pm | Sailfish petrol | 0x10, 0x18, 0x30 |
| Night | 9pm-5am | Deep blend | 0x12, 0x08, 0x1A |

## Weather Modifications

Weather conditions shift the base RGB values:

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

## Weather Data Source

Weather is fetched every 5 minutes from `wttr.in/41101` using:
```
curl -s --max-time 8 'https://wttr.in/41101?format=%t+%C+%h+%w&u'
```

Returns: temperature (F), condition, humidity (%), wind speed.
