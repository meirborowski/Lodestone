# Architecture

## Overview
- Static library for the core engine, executable(s) for editor and runtime (depending on chosen design)
- Need an editor to build games - this can be embedded into the runtime executable (and stripped from Dist builds), or can be a standalone executable
- Scenes are made of entities and components, using EnTT
- Game logic is kept separate from rendering and audio: the simulation (scripts, physics, animation, networking) must be able to run without a window, renderer or audio device. This is what makes dedicated servers and headless tests possible
- Multiplayer is server-authoritative (see [Networking](Features/Networking.md))
- Ability to "export" a game - an executable that runs the game without editing ability, which we can distribute (see [Export](Features/Export.md))
- The editor needs to be fully controllable by AI agents - I should be able to ask you to build me a game like Tetris, and you should have all the tools available to do so without my intervention (see [AI Control](AIControl.md))

## Asset System
- Every asset has a stable UUID. Scenes, prefabs and other assets reference each other by UUID, never by file path
- An asset registry maps UUIDs to files and stores import settings in a metadata file next to each asset
- Importers for glTF models (including skeletons and animation clips), textures (PNG/JPG), HDRIs (.hdr), audio (WAV/FLAC/MP3), fonts (TTF) and Lua scripts
- Hot reload in the editor for scripts, shaders and textures
- Export packs all assets used by the game into a pack file
