# Animation

## Features
- Import skeletons, skins and animation clips from glTF
- Animator component: play clips, loop, set playback speed, and crossfade between clips
- GPU skinning, with skinned meshes fully supported by shadows, SSAO and the rest of the renderer
- Controllable via scripting (play, stop, crossfade, speed, query the current clip and time)
- Preview clips in the editor, with a timeline to scrub through them
- Animation state (current clip, time, crossfade) replicates over the network (see [Networking](Networking.md))

## Out of Scope (for now)
- State machines, blend trees, IK, root motion and morph targets

## Testing
- Unit tests for clip sampling, looping and crossfade blending against known poses
- Reference-image tests of skinned meshes at fixed animation times
