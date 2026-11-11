# diepcustom v2

A from-scratch C++ reimplementation of both the diep.io game server and client.

## Timeline

- **2020:** diep.io ships the WASM client build (`6f59094d`) that everything downstream references.
- **~2022:** [diepcustom](https://github.com/abcxff/diepcustom) begins as a TypeScript server built to work with the original WASM client.
- **~2024:** diepcustom development winds down.
- **This fork:** I rewrote the entire TS server in C++ (`server/`), then built a new client from scratch in C++/Emscripten (`client/`) drawing with WebGL2 and Canvas2D, replacing the original WASM binary entirely. Both speak the old diep protocol.

## Building

uWebSockets is a submodule. After cloning, fetch it and its uSockets (its other submodules aren't needed):
```
git submodule update --init
git -C server/third_party/uWebSockets submodule update --init uSockets
```

**Server:**
```
cd server && mkdir -p build && cd build && cmake .. && make
```
Needs CMake, a C++17 compiler and zlib.

**Client:**
```
cd client && ./build.sh
```
Needs the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html) in `~/emsdk`, CMake and Ninja. The output goes to `client/dist`.

## Running

```
cd server && ./build/server
```
The server also hosts the client, so open http://localhost:8080.

Environment variables:
- `PORT`: the port to listen on (8080 by default).
- `DEV_PASSWORD_HASH`: sha256 hex of a dev password. Enter `util_set_dev_password <password>` in the console (Home) and reconnect to get full access. Without it nobody has dev access.
- `SERVER_INFO`: the host name shown next to your ping.
- `TRUST_PROXY`: how many reverse proxies sit in front of the server (0 by default). Each one adds the address it saw to `X-Forwarded-For`, and the server takes the player's IP from there. Keep it at 0 when players connect directly, or they could pick their own IP.

## Docker

```
docker compose up -d --build
```
This builds the client and server into one image and runs it, with the game on port 8080 like running it by hand. `GAME_PORT` changes the port, and `DEV_PASSWORD_HASH` is passed through.

To serve it over HTTPS, put [Caddy](https://caddyserver.com) in front:
```
docker compose -f compose.yaml -f compose.caddy.yaml up -d --build
```
By default it serves https://localhost with Caddy's own certificate. Set `SITE_ADDRESS` to your domain to get a real one (ports 80 and 443 need to be reachable). `HTTP_PORT` and `HTTPS_PORT` change its ports.

Or play through a Cloudflare quick tunnel, without opening any ports. Cloudflare handles HTTPS, so this doesn't need Caddy:
```
docker compose -f compose.yaml -f compose.tunnel.yaml up -d --build
docker compose -f compose.yaml -f compose.tunnel.yaml logs tunnel | grep trycloudflare.com
```
The second command prints the address. It changes whenever the tunnel restarts.

Other commands like `logs` and `down` need the same `-f` files, so take a setup down with its files before switching to another. To skip typing them, put `COMPOSE_FILE=compose.yaml:compose.caddy.yaml` (or `compose.tunnel.yaml`, or all three) in a `.env` file next to `compose.yaml`, along with any of the settings above.

The game container holds only the static server binary, its data and the client, and runs as a non-root user with no capabilities on a read-only filesystem. With Caddy or the tunnel it also sits on a network with no internet access and isn't published itself, so players reach it only through them. On its own it needs a published port, which Docker can't give a container on a network without internet access.

## Layout

- `server/src`: `Native` is the entity, networking and view code, `Entity` the game objects, `Gamemodes` the arenas.
- `client/src`: `sim` decodes and interpolates updates, `gl` is the WebGL2 world renderer, `cp5` a buffered Canvas2D layer for the UI and fallback drawing.
- `client/web`: the page around the client.

Add `?canvas2d` to the URL to draw the world without WebGL2.

## License

AGPL-3.0, the same as [diepcustom](https://github.com/abcxff/diepcustom), which the server started as a port of. Thanks to ABCxFF and the diepcustom contributors.
