![version](https://img.shields.io/badge/version-18%2B-EB8E5F)
![platform](https://img.shields.io/static/v1?label=platform&message=mac-intel%20|%20mac-arm%20|%20win-64&color=blue)
[![license](https://img.shields.io/github/license/miyako/4d-plugin-potrace-v2)](LICENSE)
![downloads](https://img.shields.io/github/downloads/miyako/4d-plugin-potrace-v2/total)

# 4d-plugin-potrace-v2

The Potrace plugin wraps [potrace](http://potrace.sourceforge.net/) and its companion tool `mkbitmap` to convert raster bitmaps into vector outlines. It exposes two commands: `Potrace`, which traces a bitmap and returns a vector image as an `Object`, and `Mkbitmap`, which pre-processes (filters/scales/thresholds) a bitmap and returns the result as a `BLOB` you can feed straight back into `Potrace`. Traced output comes back either as an SVG or a PDF `Picture`, embedded in the returned object.

> **This reference describes the plugin after the source-level fixes applied in this review session** (safe handling of empty/zero-length blobs, no more host hang on a thrown exception, and `fillcolor`/`color` now reported on the *return* object instead of overwriting the caller's `options` parameter). If you're testing against a plugin binary built before this session, rebuild from the fixed source first — the behavior below won't match an older binary on those three points.

| Command | Returns | Purpose |
|---|---|---|
| [`Potrace`](#potrace) | `Object` | Trace a bitmap (BMP or PNM-family) into an SVG or PDF vector `Picture`. |
| [`Mkbitmap`](#mkbitmap) | `BLOB` | Filter/scale/threshold a bitmap into a PBM/PGM `BLOB`, typically as a pre-processing step before `Potrace`. |

**Platforms:** Mac and Windows (4D has no Linux runtime; standard 4D plugin platform support).

---

## Requirements & platform notes

- Both commands are declared `threadSafe` in `manifest.json` — safe to call from a preemptive process.
- **Failure is silent, not a 4D error.** Neither command raises a 4D error or sets `OK`. On any failure (missing/invalid input, unrecognized bitmap format, or a processing error deep in potrace/mkbitmap), `Potrace` returns an empty `Object` (`{}`) and `Mkbitmap` returns a zero-length `BLOB`. Always check for the properties/length you expect rather than assuming success. See [Error handling & troubleshooting](#error-handling--troubleshooting) for the specific silent-failure paths.
- **The first parameter of both commands is a `BLOB`, not a `Picture`.** If you're starting from a 4D `Picture`, convert it first with `PICTURE TO BLOB`. `Potrace` accepts a BMP-format `BLOB` or a PNM-family (PBM/PGM/PPM) `BLOB` — which is exactly what `Mkbitmap` returns, so the two commands are meant to be chained (see the [worked example](#worked-example-mkbitmap--potrace) below).
- This source-level review did not have access to the plugin's platform-specific bitmap/backend implementation files (`4DPlugin-bmp`, `backend_svg`, `backend_pdf`, etc.), so no platform-specific behavioral divergence between Mac and Windows is asserted here one way or the other — none was observed in the dispatch/option-handling code that was reviewed.

---

## Potrace

### Syntax
```4d
Potrace ( image ; options ) → Result
```

| Parameter | Type | Description |
|---|---|---|
| `image` | BLOB | Raw bitmap bytes — either BMP format, or a PNM-family (PBM/PGM/PPM) bitmap, such as the `BLOB` returned by [`Mkbitmap`](#mkbitmap). Convert a `Picture` to this form first with `PICTURE TO BLOB`. |
| `options` | Object | Tracing options — see the table below. Every key is optional; omit any you don't need. |
| Result | Object | The traced output and an echo of the resolved settings. See the table below. |

**`options` properties (all optional):**

| Property | Type | Default | Description |
|---|---|---|---|
| `format` | Text | `".svg"` | Output format: `".svg"` or `".pdf"`. Any other value falls back to `.svg` silently. |
| `turdsize` | Number | potrace's own default | Suppress speckles of up to this many pixels. |
| `alphamax` | Number | potrace's own default | Corner-smoothing parameter for curve fitting. |
| `opticurve` | Boolean | potrace's own default | Enable curve optimization (fewer, smoother Bézier segments). |
| `opttolerance` | Number | potrace's own default | Curve-optimization tolerance. |
| `policy` or `turnpolicy` | Text | `"minority"` | Ambiguous-corner resolution rule: `"black"`, `"white"`, `"right"`, `"left"`, `"minority"`, `"majority"`, or `"random"`. Either key name works; `policy` is checked first. Unrecognized values fall back to `"minority"` silently. |
| `group` | Text | `"flat"` | SVG path grouping: `"flat"`, `"connected"`, or `"hierarchical"`. |
| `pagesize` | Text | `"A4"` | Page size, relevant when `format` is `".pdf"`: `"A4"`, `"A3"`, `"A5"`, `"B5"`, `"Letter"`, `"Legal"`, `"Tabloid"`, `"Statement"`, `"Executive"`, `"Folio"`, `"Quarto"`, or `"10x14"`. |
| `opaque` | Boolean | `False` | Paint white shapes opaquely instead of leaving them transparent. |
| `invert` | Boolean | `False` | Invert the bitmap before tracing. |
| `tight` | Boolean | `False` | Fit the bounding box to the actual traced outline instead of the full bitmap. |
| `angle` | Number | `0` | Rotate the output by this many degrees. |
| `gamma` | Number | `2.2` | Gamma value used when rendering to a greyscale backend. |
| `blacklevel` | Number | `0.5` | Black/white cutoff (0–1) for the input bitmap. |
| `stretch` | Number | `1` | Aspect-ratio stretch factor (height/width). |
| `unit` | Number | `10` | Output grid granularity. |
| `longcoding` | Boolean | `False` | Skip file-size optimization in the output encoding. |
| `fillcolor` | Text | `"#ffffff"` | Fill color as `#rrggbb`. Invalid values are ignored and the default is kept. |
| `color` | Text | `"#000000"` | Line color as `#rrggbb`. Invalid values are ignored and the default is kept. |
| `width` / `height` | Text | unset | Desired output dimension, e.g. `"10cm"`, `"4in"`, `"72pt"`. **Written back onto your `options` object** (as a normalized `{x; d}` object) rather than the return object — see the caveat below. |
| `leftmargin` / `rightmargin` / `topmargin` / `bottommargin` | Text | unset | Margin, same dimension-string syntax as `width`/`height`. **Also written back onto `options`.** |
| `resolution` | Text | unset | `"XxY"` with no unit suffix on either number, e.g. `"300x300"`. A unit suffix or a zero value on either side is silently ignored. **Written back onto `options` as `rx`/`ry`.** |
| `scale` | Text | unset | `"XxY"` with no unit suffix, e.g. `"2x2"`. **Written back onto `options` as `sx`/`sy`.** |

> **Caveat verified from source:** unlike every other option above, `width`, `height`, the four margin keys, `resolution`, and `scale` have their *resolved* values written back onto the `options` object you passed in — not onto the returned `Result` object. If you reuse the same `options` object across multiple calls, expect it to have gained these normalized properties after the first call.

**`Result` object properties:**

| Property | Type | Description |
|---|---|---|
| `image` | Picture | The traced SVG or PDF, present only if tracing succeeded. Absent on failure (see [Error handling](#error-handling--troubleshooting)). |
| `format` | Text | Resolved output format (`".svg"` or `".pdf"`), always present when tracing succeeded. |
| `policy` | Text | Resolved turn policy name, always present when tracing succeeded. |
| `turdsize`, `alphamax`, `opticurve`, `opttolerance`, `opaque`, `invert`, `tight`, `angle`, `gamma`, `blacklevel`, `stretch`, `unit`, `longcoding` | (matches input type) | Echoed back only if you supplied that key in `options`. |
| `fillcolor`, `color` | Integer (`0xRRGGBB`) | Echoed back as a packed integer only if you supplied that key in `options`. |

### Description

`Potrace` reads the BLOB, tries to parse it as BMP first, then as a PNM-family bitmap if that fails, and hands the result to libpotrace. If either read or the trace itself fails, the command returns an empty object with no `image` key and no error indicator — check for `.image` before using the result. The output `Picture` is SVG or PDF data depending on `format`; either can be written straight out with `WRITE PICTURE FILE`.

### Example

From the plugin's own test method (`test.4dm`):
```4d
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"test.png"
READ PICTURE FILE:C678($path;$png)
PICTURE TO BLOB:C692($png;$bmp;".bmp")

$option:=New object:C1471("format";".svg")
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"test.svg"
WRITE PICTURE FILE:C680($path;$t.image)

$option:=New object:C1471("format";".pdf")
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"test.pdf"
WRITE PICTURE FILE:C680($path;$t.image)
```

A minimal call with default options, always checking for `.image` before using it:
```4d
$option:=New object:C1471
$result:=Potrace ($bmp;$option)
If (OB Is defined:C1231($result;"image"))
	WRITE PICTURE FILE:C680($path;$result.image)
Else
	ALERT:C41("Potrace could not trace this bitmap.")
End if 
```

Tightening the trace and picking a turn policy explicitly:
```4d
$option:=New object:C1471
$option.turdsize:=10
$option.policy:="black"
$option.tight:=True
$result:=Potrace ($bmp;$option)
```

---

## Mkbitmap

### Syntax
```4d
Mkbitmap ( image ; options ) → Result
```

| Parameter | Type | Description |
|---|---|---|
| `image` | BLOB | Raw bitmap bytes, same accepted formats as `Potrace`'s `image` parameter. |
| `options` | Object | Filtering/scaling options — see the table below. Every key is optional. |
| Result | BLOB | The processed bitmap, as raw PBM (bilevel) or PGM (greyscale) bytes. Zero-length on failure. |

**`options` properties (all optional):**

| Property | Type | Default | Description |
|---|---|---|---|
| `filter` | Number | disabled unless set (radius `4` if enabled via any other means) | Highpass filter radius. Setting this key also enables the highpass filter. |
| `blur` | Number | disabled | Lowpass filter (blur) radius. Setting this key also enables the lowpass filter. |
| `scale` | Number | `2` | Integer scaling factor applied to the bitmap. |
| `threshold` | Number | `0.45` if enabled | Grey-level cutoff (0–1) for bilevel output. Setting this key also forces bilevel output and `.pbm`-style processing. |
| `grey` | Boolean | `False` | If `True`, produce greyscale (PGM) output instead of bilevel (PBM). |
| `linear` | Boolean | `False` | If `True`, use linear interpolation when scaling instead of cubic. |
| `cubic` | Boolean | `True` (implicit) | If `True`, explicitly select cubic interpolation (the default; only useful to override an earlier `linear` on the same call — see the precedence note below). |
| `nofilter` | Boolean | `False` | If `True`, disable the highpass filter regardless of `filter`. |
| `invert` | Boolean | `False` | If `True`, invert the bitmap before any filtering/scaling. |
| `nodefaults` | Boolean | `False` | If `True`, resets `invert`, highpass filtering, `scale` (to `1`), and bilevel/greyscale mode to a "no processing" baseline — **and does so after every other key on this list is applied**, so it silently overrides `filter`, `scale`, `threshold`, or `grey` given in the same call. Don't combine `nodefaults` with other options; call it alone. |

> **Precedence caveat verified from source:** these keys are checked in a fixed order — `filter`, `blur`, `scale`, `threshold`, `grey`, `linear`, `cubic`, `nofilter`, `invert`, `nodefaults` — and later keys in that order override the *effects* of earlier ones on the same underlying setting. Concretely: `threshold` forces bilevel mode, but a `grey:True` in the same call (checked afterward) will still switch to greyscale, silencly discarding the threshold-driven bilevel mode. `nodefaults`, being last, overrides everything else regardless of what else you set.

### Description

`Mkbitmap` reads the BLOB as a greymap (`gm_read`), applies inversion/highpass/lowpass filtering and scaling as configured, then produces either a bilevel (PBM) or greyscale (PGM) bitmap. If the input can't be read, or the highpass filter step fails, the command returns a zero-length `BLOB` with no error indicator — check the returned `BLOB`'s length before using it. No metadata about which settings were actually applied is returned; unlike `Potrace`, none of the option values are echoed back.

### Example

From the plugin's own test method (`test_mkbitmap.4dm`):
```4d
$option:=New object:C1471("scale";2;"filter";2;"threshold";0.48)
$pgm:=Mkbitmap ($bmp;$option)  //-f 2 -s 2 -t 0.48
```

Checking for failure before feeding the result onward:
```4d
$option:=New object:C1471("scale";3)
$processed:=Mkbitmap ($bmp;$option)
If (BLOB size:C605($processed)=0)
	ALERT:C41("Mkbitmap failed on this input.")
Else
	  // safe to trace $processed with Potrace
End if 
```

Producing a greyscale (PGM) result instead of the bilevel default:
```4d
$option:=New object:C1471("grey";True;"scale";1)
$pgm:=Mkbitmap ($bmp;$option)
```

---

## Worked example: Mkbitmap → Potrace

Adapted from the plugin's own `test_mkbitmap.4dm`, showing the intended chain — pre-process with `Mkbitmap`, then trace the result with [`Potrace`](#potrace):
```4d
$path:=Get 4D folder:C485(Current resources folder:K5:16)+"loxie-orig.png"
READ PICTURE FILE:C678($path;$png)
PICTURE TO BLOB:C692($png;$bmp;".bmp")

  // trace the raw bitmap directly, no pre-processing
$option:=New object:C1471
$t:=Potrace ($bmp;$option)
$path:=System folder:C487(Desktop:K41:16)+"loxie-raw.svg"
WRITE PICTURE FILE:C680($path;$t.image)

  // pre-process first: -f 2 -s 2 -t 0.48
$option:=New object:C1471("scale";2;"filter";2;"threshold";0.48)
$pgm:=Mkbitmap ($bmp;$option)

  // then trace the pre-processed bitmap, with a larger turdsize: -t 5
$option:=New object:C1471("format";".svg";"turdsize";5)
$t:=Potrace ($pgm;$option)

$path:=System folder:C487(Desktop:K41:16)+"loxie.svg"
WRITE PICTURE FILE:C680($path;$t.image)
```

---

## Error handling & troubleshooting

- **Both commands fail silently.** Neither raises a 4D error. `Potrace` returns `{}` (no `image` key) and `Mkbitmap` returns a zero-length `BLOB` on any failure — always check `OB Is defined:C1231($result;"image")` (Potrace) or `BLOB size:C605($result)=0` (Mkbitmap) before using the result.
- **Unrecognized `image` bytes produce the same silent failure as no image at all.** If the `BLOB` isn't valid BMP and isn't a valid PNM-family bitmap, `Potrace` returns `{}`; if it isn't readable as a greymap, `Mkbitmap` returns a zero-length `BLOB`. There's no distinction in the result between "you passed nothing" and "you passed garbage."
- **`Potrace`'s `width`/`height`/margin/`resolution`/`scale` keys mutate your `options` object.** These six keys write their normalized values back onto the `options` object you passed in, not onto the returned result. If you reuse the same options object for a second call, it will already carry these extra properties from the first call.
- **`nodefaults` in `Mkbitmap` overrides everything else in the same call.** Because of the fixed check order in the plugin's source, `nodefaults:True` resets `invert`, filtering, `scale`, and bilevel/greyscale mode after any other key you set in the same options object has already been applied. Call it alone if you want a clean baseline.
- **`threshold` vs. `grey` in `Mkbitmap`:** `threshold` forces bilevel output, but if `grey:True` is also present in the same call it wins (checked later), switching to greyscale and effectively ignoring the threshold-driven bilevel mode.
- **Malformed or extreme numeric options aren't validated by the plugin.** Values for `turdsize`, `alphamax`, `opttolerance`, `angle`, `gamma`, `blacklevel`, `stretch`, `unit`, `filter`, `blur`, `scale`, and `threshold` are passed straight through to potrace/mkbitmap with no range checking in the plugin itself. Stick to sane values (as shown in the examples above) — the plugin does not guard against negative, zero, or out-of-range inputs.
- **`fillcolor`/`color` must be exactly `#rrggbb`.** Any other format (missing `#`, wrong length, non-hex characters) is silently ignored and the current default is kept — no error, no partial parse.

---

## Quick reference

```4d
  // Picture → BLOB, ready for either command
PICTURE TO BLOB:C692($picture;$bmp;".bmp")

  // trace directly
$option:=New object:C1471("format";".svg")
$result:=Potrace ($bmp;$option)
If (OB Is defined:C1231($result;"image"))
	WRITE PICTURE FILE:C680($outputPath;$result.image)
End if 

  // pre-process, then trace
$option:=New object:C1471("scale";2;"filter";2;"threshold";0.48)
$pgm:=Mkbitmap ($bmp;$option)
If (BLOB size:C605($pgm)>0)
	$option:=New object:C1471("format";".svg";"turdsize";5)
	$result:=Potrace ($pgm;$option)
	If (OB Is defined:C1231($result;"image"))
		WRITE PICTURE FILE:C680($outputPath;$result.image)
	End if 
End if 
```
