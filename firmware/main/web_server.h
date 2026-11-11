#pragma once

/**
 * HTTP server on port 80:
 *   GET    /                 the web page (also the browser version of the game)
 *   GET    /api/state        game status and the LED colours, as JSON
 *   GET    /api/highscores   the table and the current player
 *   POST   /api/player       {"name": "..."}  set the current player
 *   POST   /api/press        {"color": "red" | "yellow" | "blue"}  press a button
 *   DELETE /api/highscores   clear the table
 *   GET    /api/palette      the colours ("rrggbb" per name) and the defaults
 *   POST   /api/palette      {"orange": "ff4600", ...}  change colours (any subset)
 *   POST   /api/palette/save keep the colours across reboots
 *   POST   /api/palette/reset back to the defaults
 *   POST   /api/preview      {"on": true}  show the colour chart for 5 s more
 */
void web_server_start(void);
