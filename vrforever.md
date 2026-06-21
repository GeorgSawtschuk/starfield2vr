# Problemstellung

Aktuell gibt es mit dem Mod ein Problem, dass beim Umschalten von VR zu flat und zurueck (wenn zum Beispiel Inventar oder Map abgerufen wird) gehen ca 5Gb RAM verloren. Die Vermutung ist, das passiert in dem Spiel selbst und ist nicht von dem VR Mod kontrollierbar. Die Idee ist, immer in VR Mode bleiben, auch wenn zum Beispiel Map oder Inventar aufgerufen werden. Bloss fuer solche Fälle, keine Ahnung, Kamera Sicht aendern, dass das "flat"-Screen komplet überblickbar ist. Als ob man in einem leeren VR Raum ist und Screens schweben in der Luft?

Als moeglicher Hinweis/Ansatz. Es wird bereits von dem Mod der Overlay, mit Life-Indikator, Stamina-Indikator, Radar usw halb sichtbar dargestellt. Kann man eventuell auf aenlicher Weise die restlichen Screens darstellen? 

# Moegliche Ansätze

## Wie der aktuelle Modus-Wechsel funktioniert

Der Mod wechselt **nicht** den Render-Modus des Spiels. Starfield rendert immer in VR-Auflösung.
Was sich ändert wenn ein schweres Menü (Inventory, Map, etc.) geöffnet wird:

1. `onScaleformSetViewPort` → `GameFlow::renderMenu()` zählt das Menü als "schwer" → `rendered_menus_count > 0`
2. `RenderGraphStart` setzt `ModSettings::g_internalSettings.showQuadDisplay = true`
3. `ModSettings::showFlatScreenDisplay()` gibt `true` zurück (nur bei OpenXR; bei OpenVR immer `false`)
4. OpenXR `end_frame()` sendet statt zwei `XrCompositionLayerProjection`-Views einen einzigen `XrCompositionLayerQuad` (flacher Screen im VR-Raum)
5. `UpdateWorldCamera()` und `onFPSGetCameraRotation()` deaktivieren den Head-Tracking-Override

Der RAM-Verlust entsteht wahrscheinlich im Spiel selbst — vermutlich allokiert die Creation Engine bei Menüöffnung grosse Scaleform-Render-Targets (oder reagiert auf den Kamera-Zustandswechsel) und gibt den Speicher nicht sauber frei.

---

## Ansatz 1: `showQuadDisplay` dauerhaft deaktivieren (Diagnosetst)

**Idee:** In `CreationEngineRendererModule.cpp` die Zeile
```cpp
ModSettings::g_internalSettings.showQuadDisplay = GameFlow::isShowingMenu();
```
durch `= false` ersetzen. Damit bleibt OpenXR immer in Stereo-Projektion.

**Ergebnis:** Zeigt ob der RAM-Verlust durch den OpenXR-Modus-Wechsel selbst ausgelöst wird
oder ob er spielseitig unabhängig davon entsteht. Menüs werden dann weiter in Stereo angezeigt
(etwas verzerrt, aber funktionsfähig). Scaleform-Viewport-Anpassungen (`onScaleformSetViewPortInternal`)
bleiben aktiv, da sie auf `!showFlatScreenDisplay()` prüfen.

**Aufwand:** Minimal — eine Zeile. Guter erster Schritt.

---

## Ansatz 2: Kamera-Zoom-Out im Stereo-Modus ("Kino-Raum")

**Idee:** Statt den Head-Tracking-Override zu deaktivieren, wenn ein Menü erscheint,
wird die Kamera auf eine feste "Menü-Position" gesetzt: ein paar Meter zurück, leicht nach unten
geneigt, so dass der flache Spielbildschirm wie ein schwebender Monitor im leeren VR-Raum wirkt.
OpenXR bleibt dauerhaft in Stereo-Modus (`showFlatScreenDisplay()` immer `false`).

**Wo im Code:**
- `UpdateWorldCamera()` in `CreationEngineCameraManager.cpp` (Zeile 272): Hier wird bereits
  per `showFlatScreenDisplay()` verzweigt. Stattdessen könnten wir bei `isShowingMenu() == true`
  einen festen Offset-Transform anwenden (z.B. -2 m auf Z, keine HMD-Rotation).
- `onFPSGetCameraRotation()` (Zeile 319): Analog, kein Yaw-Update bei offenem Menü.
- `getMenuSettings()` in `GameFlow.cpp` setzt `perspective = 0` für Menüs — das bleibt erhalten.

**Vorteil:** Das Spiel ändert seinen Render-Pfad nie — es rendert immer für Stereo-VR.
Die Vermutung ist, dass genau der spielseitige Zustandswechsel (ausgelöst durch das bisherige
Deaktivieren der Kamera/Head-Tracking) die RAM-Allokation triggert.

**Aufwand:** Mittel. `UpdateWorldCamera()` und `onFPSGetCameraRotation()` bekommen je
einen dritten Branch für `isShowingMenu() == true`. Dazu sollte `showQuadDisplay` dauerhaft
`false` bleiben (→ kein OpenXR-Modus-Wechsel mehr).

---

## Ansatz 3: Bestehenden Quad-Layer verbessern (OpenXR only)

Der vorhandene `XrCompositionLayerQuad`-Pfad funktioniert schon (für OpenXR).
Es gibt zwei bekannte Lücken:

- **OpenVR-Lücke:** `showFlatScreenDisplay()` gibt für OpenVR immer `false` zurück
  (`//TODO fix for openvr` in `ModSettings.cpp` Zeile 12). OpenVR-User haben damit keine
  Flat-Screen-Ansicht und leiden möglicherweise besonders unter dem RAM-Leak.
- **Positionierung:** Der Quad wird via `m_center_stage` und `m_flat_screen_distance`
  positioniert (OpenXR: `runtimes/OpenXR.cpp` Zeile 1472–1477). Die aktuelle Grösse ist
  2 m breit fix — kein Aspect-Ratio-Ausgleich auf Breite/Höhe, kein konfigurierbarer
  Vertikaloffset. Das könnte verbessert werden damit der Screen auf Augenhöhe schwebt.

**Aufwand:** Klein bis mittel. Löst aber nicht das eigentliche RAM-Problem — der spielseitige
Auslöser bleibt unberührt.

---

## Empfehlung

Reihenfolge: **Ansatz 1** zuerst (ein-Zeilen-Test um den Auslöser einzugrenzen),
dann je nach Ergebnis **Ansatz 2** (wahrscheinlich die eigentliche Lösung).
Ansatz 3 ist unabhängig davon sinnvoll als Verbesserung für OpenVR-Nutzer.

# Technische Umsetzung



