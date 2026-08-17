# OpenWorldMCP

Eigene MCP-Toolsets für Unreal Engine 5.8 – registriert über die offizielle `ToolsetRegistry`. Jede `UFUNCTION(meta=(AICallable))` in einem `UToolsetDefinition` wird automatisch als MCP-Tool auf Epics eingebautem MCP-Server (`http://127.0.0.1:8000/mcp`) exponiert.

## Voraussetzungen

- Unreal Engine 5.8
- Offizielle MCP-Plugins aktiviert: `ModelContextProtocol`, `ToolsetRegistry`, `AllToolsets` (Editor Tools)

## Installation

1. Ordner in `Plugins/OpenWorldMCP` deines Projekts kopieren
2. In der `.uproject` aktivieren:
   ```json
   { "Name": "OpenWorldMCP", "Enabled": true }
   ```
3. Projekt neu kompilieren (der Editor baut das Plugin beim Start)
4. Editor starten → Toolset erscheint unter `list_toolsets`

## Toolsets

### OpenWorldFoliageService

Crash-freies Platzieren von Foliage auf der Landschaft als `InstancedStaticMesh` (ein Actor, viele Instanzen) – mit Physics-Trace aufs Terrain für Höhe + Normalen-Ausrichtung.

| Tool | Beschreibung |
|---|---|
| `ScatterOnLandscape` | Streut `count` Instanzen in einem Kreis (Zentrum, Radius) auf die Landschaft |
| `ScatterRectOnLandscape` | Streut `count` Instanzen in einem Rechteck auf die Landschaft |

Beispiel-Aufruf:

```
ScatterOnLandscape(
  meshPath: "/PCG/SampleContent/SimpleForest/Meshes/PCG_Tree_01",
  worldCenterX: 30000, worldCenterY: -10000,
  radius: 12000, count: 60,
  minScale: 0.8, maxScale: 1.2,
  bAlignToNormal: true, bRandomYaw: true, seed: 42)
```

## Eigene Toolsets erweitern

Neue Toolset-Klasse in `Source/OpenWorldMCP/Public/` anlegen, von `UToolsetDefinition` erben und Methoden mit `meta = (AICallable)` markieren. Die Registrierung passiert automatisch über `FOpenWorldMCPModule::RegisterToolsets()` (verzögert auf `PostEngineInit`, weil die ToolsetRegistry erst dann verfügbar ist).

## Lizenz

MIT – siehe [LICENSE](LICENSE).
