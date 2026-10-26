# diepcustom v2

A from-scratch C++ reimplementation of both the diep.io game server and client.

## Timeline

- **2020:** diep.io ships the WASM client build (`6f59094d`) that everything downstream references.
- **~2021:** [diepcustom](https://github.com/abcxff/diepcustom) begins as a TypeScript server built to work with the original WASM client.
- **~2024:** diepcustom development winds down.
- **This fork** I rewrote the entire TS server in C++ (`server/`), then built a new client from scratch in C++/Emscripten (`client/`) drawing with WebGL2 and Canvas2D, replacing the original WASM binary entirely. Both speak the old diep protocol.

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

## Layout

- `server/src`: `Native` is the entity, networking and view code, `Entity` the game objects, `Gamemodes` the arenas.
- `client/src`: `sim` decodes and interpolates updates, `gl` is the WebGL2 world renderer, `cp5` a buffered Canvas2D layer for the UI and fallback drawing.
- `client/web`: the page around the client.

Add `?canvas2d` to the URL to draw the world without WebGL2.

## License

AGPL-3.0, the same as [diepcustom](https://github.com/abcxff/diepcustom), which the server started as a port of. Thanks to ABCxFF and the diepcustom contributors.
