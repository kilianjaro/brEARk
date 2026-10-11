# Fonts

All fonts are under the SIL Open Font License 1.1 (see the `OFL-*.txt` files); neither declares a Reserved Font Name.

| File | Source | Used for |
|---|---|---|
| `SpaceGrotesk-Variable.ttf` | [google/fonts `ofl/spacegrotesk/SpaceGrotesk[wght].ttf`](https://github.com/google/fonts/tree/main/ofl/spacegrotesk) | Source of the Bold below (not embedded) |
| `SpaceGrotesk-Bold.ttf` | Static instance at weight 700, derived from the variable font | The poster word, the brEARk mark |
| `SpaceMono-Regular.ttf`, `SpaceMono-Bold.ttf` | [google/fonts `ofl/spacemono/`](https://github.com/google/fonts/tree/main/ofl/spacemono) | All data and text |

Why a static Bold: JUCE can't select a variation axis, and the variable font's default is Light (300).

Regenerate it with:

```bash
fonttools varLib.instancer SpaceGrotesk-Variable.ttf wght=700 --update-name-table -o SpaceGrotesk-Bold.ttf
```
