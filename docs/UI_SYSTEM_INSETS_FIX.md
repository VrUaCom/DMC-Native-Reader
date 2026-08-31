# UI system insets fix

The existing Native Reader UI remains unchanged. This fix only establishes the Android safe drawing bounds so app content does not overlap system UI.

## Boundary

- top content is padded below the status bar / display cutout
- bottom action bar is padded above the navigation bar / gesture inset
- left/right system insets are respected
- existing status text, render view and Open / Reset / Wire controls are otherwise unchanged

## Reason

With modern Android edge-to-edge behavior (notably targetSdk 35+), an Activity cannot assume that its content origin begins below system bars. The root view must consume the current `WindowInsets` and apply them to its content bounds.

## Compatibility

- API 30+: `WindowInsets.Type.systemBars()` + `displayCutout()`
- API 26-29: legacy `getSystemWindowInset*()` fallback

No format-decoder, routing, rendering or native-library behavior is changed by this patch.
