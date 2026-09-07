# sound

Standalone audio loading and playback that does **not** need a game window.

```c
import("sound");
```

The module is implemented in `lynxer/stdlib/sound.lynx` on top of
[`arcade`](https://pypi.org/project/arcade/), so `pip install arcade` is
required. Every function works from source, imported modules, bytecode, and
bundled programs.

If you are already using `global.game`, prefer `global.game.loadSound` — this
module exists for programs that only want audio, without creating a window.

## Handles

Loading a file returns an `int` **handle**. Handles are stable indices:

- A failed load returns `-1`.
- Passing an out-of-range or released handle to any function is safe: boolean
  functions return `false`, `getSoundLength` returns `0.0`,
  `isSoundPlaying` returns `false`.
- `releaseSound` invalidates a handle; releasing twice is safe (the second call
  returns `false`).

```c
int music = global.sound.loadSound("assets/theme.wav");
if(music < 0){ println("could not load the file"); }
```

Relative paths are resolved by `arcade` against the process working directory,
not against the source file. Pass an absolute path when the working directory
is not predictable (for example inside a bundled program).

## Functions

| Function | Returns | Description |
|----------|---------|-------------|
| `loadSound(str path)` | `int` | Load a file fully into memory. `-1` on failure. |
| `loadSoundStreaming(str path)` | `int` | Load a file for streaming playback. `-1` on failure. |
| `playSound(int handle)` | `bool` | Play once, restarting if already playing. |
| `playSoundOnce(int handle)` | `bool` | Alias of `playSound`. |
| `loopSound(int handle)` | `bool` | Play and repeat until stopped. |
| `stopSound(int handle)` | `bool` | Stop playback and clear the active player. |
| `pauseSound(int handle)` | `bool` | Pause the active player. |
| `resumeSound(int handle)` | `bool` | Resume a paused player. |
| `setSoundVolume(int handle, float volume)` | `bool` | Set volume, clamped to `0.0 .. 1.0`. |
| `isSoundPlaying(int handle)` | `bool` | Whether a player is currently playing. |
| `getSoundLength(int handle)` | `float` | Duration in seconds; `0.0` for an invalid handle. |
| `releaseSound(int handle)` | `bool` | Stop and free the sound; invalidates the handle. |
| `soundCount()` | `int` | Number of sound slots currently allocated. |

## Example

```c
import("sound");

global setup(){}
global main(){
    int music = global.sound.loadSound("theme.wav");
    if(music >= 0){
        global.sound.setSoundVolume(music, 0.5);
        global.sound.loopSound(music);
        println(global.sound.getSoundLength(music));
        global.sound.stopSound(music);
        global.sound.releaseSound(music);
    }
    println(global.sound.soundCount());
}
```

## Behaviour and limitations

- Any missing file, unsupported format, missing `arcade` package, or audio
  backend failure is reported as a return value (`-1` / `false`), never as a
  Lynxer runtime error.
- A headless machine with no audio device fails at load time, so
  `loadSound` returns `-1` and the rest of the API stays safe to call.
- Supported formats are whatever `arcade` can decode (WAV, OGG, MP3, FLAC
  depending on the installed back end).
- Playing a handle again restarts it with the volume last set through
  `setSoundVolume`; that volume is remembered for the lifetime of the handle.
- Sounds are released when the interpreter exits. Call `releaseSound` for
  long-running programs that load many files, since loaded sounds stay in
  memory for the whole process.
