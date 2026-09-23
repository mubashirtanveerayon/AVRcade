# ATmega32 U8g2 Game Engine

This engine targets the current 3.3 V ATmega32A handheld wiring and the
SSD1309 OLED over hardware SPI. The default loop runs at approximately 30 FPS.

## Layout

- `engine_config.h`: wiring, frame rate, display variant, buffer size and ADC thresholds.
- `engine.h`: public API used by games.
- `engine.c`: fixed-rate main loop.
- `engine_platform.c`: U8g2, SPI, Timer0, buttons, joystick ADC and buzzer.
- `example_game.c`: small moving-ball demonstration.
- `build_game.sh`: builds engine plus any number of game C files into one `.BIN`.

Keep the official `u8g2` repository beside the `game_engine` directory:

```text
project/
├── u8g2/
│   └── csrc/
└── game_engine/
    ├── build_game.sh
    ├── engine.c
    ├── engine_platform.c
    └── ...
```

## Build the example

```bash
cd game_engine
chmod +x build_game.sh
./build_game.sh DEMO example_game.c
```

This creates `DEMO.BIN` in the current directory. Copy it to the SD card root.

## Build a multi-file game

```bash
./build_game.sh PONG pong.c ball.c collision.c sound.c
```

The game sources must implement exactly these functions and must not define
their own `main`:

```c
void game_init(void);
void game_update(const EngineInput *input, uint16_t delta_ms);
void game_render(u8g2_t *display);
```

`game_update` executes once per frame. With page buffering, `game_render` runs
multiple times per frame, so it must only draw and must never update state.

The default `_2` display mode uses a 256-byte buffer. Change
`ENGINE_DISPLAY_BUFFER_MODE` in `engine_config.h` to `1` for a 128-byte buffer,
or `0` for the 1024-byte full buffer.

The build rejects binaries over 28,672 bytes by default, reserving the upper
4 KB for the largest ATmega32 boot section. Override only after finalizing the
boot-section size:

```bash
MAX_APPLICATION_BYTES=30720 ./build_game.sh PONG pong.c ball.c
```

## Controls and Pause Support

- **Button A**: Pin 14 (PD0) &rarr; `ENGINE_BUTTON_A` (bitmask `(1 << 0)`)
- **Button X**: Pin 15 (PD1) &rarr; `ENGINE_BUTTON_X` (bitmask `(1 << 1)`)
- **Button Y**: Pin 16 (PD2) &rarr; `ENGINE_BUTTON_Y` (bitmask `(1 << 2)`)
- **Button B**: Pin 20 (PD6) &rarr; `ENGINE_BUTTON_B` (bitmask `(1 << 3)`)
- **Joystick Switch**: Pin 18 (PD4) &rarr; `ENGINE_JOYSTICK_BUTTON` (bitmask `(1 << 4)`)
- **Pause Button**: Pin 17 (PD3 / INT1) &rarr; `ENGINE_BUTTON_PAUSE` (bitmask `(1 << 5)`)

### Pause Behavior
The Pause button triggers external interrupt `INT1` on falling edge (with 200 ms software debounce) to toggle game pause. When paused:
- `game_update()` is suspended and audio tone generation is stopped.
- The engine automatically renders a centered `PAUSED` banner over the display.
- Games can query or modify pause state programmatically via `engine_is_paused()`, `engine_set_paused()`, and `engine_toggle_pause()`.
- Legacy defines `ENGINE_BUTTON_1` through `ENGINE_BUTTON_4` are retained as aliases for backwards compatibility.

