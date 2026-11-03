#pragma once

/**
 * HTTP server on port 80:
 *   GET    /                 the web page (also the browser version of the game)
 *   GET    /api/state        game status and the LED colours, as JSON
 *   GET    /api/highscores   the table and the current player
 *   POST   /api/player       {"name": "..."}  set the current player
 *   POST   /api/press        {"color": "red" | "yellow" | "blue"}  press a button
 *   DELETE /api/highscores   clear the table
 */
void web_server_start(void);
