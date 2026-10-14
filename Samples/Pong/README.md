# Pong

The classic, on a field seen from above: you play the left paddle, the computer the right. The first to 5 points wins; Space starts a new match.

An agent built it through the editor's MCP tools alone, for [Milestone 8](../../docs/Milestones.md#8-scripting): it imported the shapes, wrote the scripts with `script_write`, built the scene and the score marker prefab with the entity and component tools, and playtested it with `input_send`, `play_step`, `log_read` and screenshots. `Tests/Editor/SampleGameTests.cpp` plays it from start to finish.

## Playing
Open this directory as a project in the editor (`LodestoneEditor --project Samples/Pong`), open `Scenes/Pong.lscene` and press play. W and S move your paddle. The score is the row of markers above the field, yours on the left; the `Status` entity's name in the hierarchy says what's happening.

## How It Works
- `Scripts/Game.lua`, on the `Game` entity - the match: serves, scoring (a marker per point, instanced from `Prefabs/ScoreMarker.lprefab`), the end, and starting again
- `Scripts/Paddle.lua`, on each paddle - moved by its player's keys, or by the computer, which follows the ball and aims its returns, but moves slower than the ball can
- `Scripts/Ball.lua` - a dynamic body without gravity or friction that bounces off the walls by itself; a paddle sends it back at an angle that depends on where it hit, a little faster each time
- `Scripts/Goal.lua`, on the trigger behind each paddle - the ball reaching it scores for the other side

The shapes are `Models/Primitives.glb`, from the engine's test assets. `.luarc.json` and `.lodestone/Lodestone.lua` let editors with lua-language-server complete and check the scripts.
