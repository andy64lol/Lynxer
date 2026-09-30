# Sound Module

The `sound` module provides audio playback functionality using Rust's `rodio` and `cpal` crates.

> **Requires:** a Rust toolchain (`cargo`) to build it, and an ALSA sound card
> to play. Loading, decoding, handle bookkeeping and the error paths work
> without a device; playback simply reports "did not start". `make testLynxer`
> skips `stdlib_sound.lynx`, with a printed message, on hosts without
> `/dev/snd/controlC*`.

## Functions

- `loadSound(path: string) -> int`
  Loads an audio file (WAV, OGG, MP3 or FLAC) **into memory**: it is decoded
  here, so playback no longer needs the file and the length is known
  immediately. Returns a stable handle, or `-1` on failure.

- `loadSoundStreaming(path: string) -> int`
  Loads a **streaming** audio file: only the header is read now and the file is
  decoded as it plays, which keeps a large file out of memory but means the file
  must still be there at play time. Returns a stable handle, or `-1` on failure.

- `playSound(handle: int) -> bool`
  Plays a loaded sound once. Returns `true` when playback starts.

- `playSoundOnce(handle: int) -> bool`
  Alias for `playSound`.

- `loopSound(handle: int) -> bool`
  Starts looping playback. Returns `true` when playback starts.

- `stopSound(handle: int) -> bool`
  Stops playback. Returns `true` when the handle was valid.

- `pauseSound(handle: int) -> bool`
  Pauses an active player. Returns `false` when the handle is invalid.

- `resumeSound(handle: int) -> bool`
  Resumes a paused player. Returns `false` when the handle is invalid.

- `setSoundVolume(handle: int, volume: float) -> bool`
  Sets the volume, clamped to `[0, 1]`. Returns `true` on success.

- `isSoundPlaying(handle: int) -> bool`
  Returns `true` if an active player is currently playing.

- `getSoundLength(handle: int) -> float`
  Returns the duration in seconds, or `0.0` for an invalid handle. The duration
  is resolved once, when the handle is created, so this never re-reads the file —
  it still answers after the file has been moved or deleted.

- `releaseSound(handle: int) -> bool`
  Releases a loaded sound and invalidates its handle. Returns `true` on success.

- `soundCount() -> int`
  Returns the number of currently loaded sound handles.

## Example

```lynx
import("sound")

global main(){
    int music = global.sound.loadSound("theme.wav");
    if(music >= 0){
        global.sound.setSoundVolume(music, 0.5);
        global.sound.loopSound(music);
        println(global.sound.getSoundLength(music));
        global.sound.stopSound(music);
        global.sound.releaseSound(music);
    }
}
```