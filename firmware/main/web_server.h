#pragma once

/**
 * HTTP server on port 80. Pages (one bundled file, the path picks the view):
 *   /            watch: the strip live and the high scores
 *   /play        play from a phone (needs the player password)
 *   /settings    settings (needs the settings password)
 *   /practice    the game in the browser
 *   /ws          WebSocket: the state below, pushed every 100 ms
 *
 * Public:
 *   GET  /api/state, /api/highscores, /api/auth
 *   POST /api/login {role, password}, /api/logout
 *   POST /api/password {role, password}   (first settings password only)
 * Player:
 *   GET  /api/queue (heartbeat), POST /api/queue {name, accept_suffix}, DELETE /api/queue
 *   POST /api/press {color}   (the current player only)
 * Settings password:
 *   GET/POST /api/settings, POST /api/settings/reset, POST /api/restart
 *   POST /api/password, POST /api/queue/admin {action, index}, POST /api/game/abort
 *   GET/POST /api/palette, POST /api/palette/save|reset, POST /api/preview
 *   DELETE /api/highscores, POST /api/ota (raw firmware image)
 */
void web_server_start(void);

/** Push the current state to the WebSocket clients (called by the game task). */
void web_server_broadcast(void);
