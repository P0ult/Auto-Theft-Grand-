// Puts the game's systems together in the browser game's order (src/main.js boot) and fills the streets
// with traffic and people. The renderer and the native tests both call these, so they run the same game.
#pragma once

namespace atg {

class Game;

// registers the systems ported so far, in main.js order, and sets the game's pointers to them
void InstallSystems(Game& game);
// main.js startGame: the player visible, the camera free, the game rules running
void StartGame(Game& game);
// main.js after the start: traffic at 70% and pedestrians at 60% of their maximum
void PopulateWorld(Game& game);

} // namespace atg
