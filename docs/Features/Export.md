# Export

- Exports for the host platform only - no cross-compiling
- Output is a folder containing the Dist runtime executable (no editor code) and the asset pack, ready to zip and distribute
- The output includes THIRD_PARTY_LICENSES.md - licenses such as MIT and BSD require their notices to ship with binary distributions
- The exported game opens the project's startup scene, using the project's window settings (resolution, fullscreen)
- Multiplayer games can also be exported as a dedicated server build: headless, with no window, renderer or audio (see [Networking](Networking.md))
- Export is available from the editor UI, MCP, and the command line
