//! Lynxer `sound` stdlib backend: audio loading and playback.
//!
//! Replaces the Python Arcade backend with Rust `rodio`/`cpal`.
//! The Lynxer-facing contract matches `clynxer/stdlib/sound.lynx`:
//! handles are integer indices into a module-local registry,
//! and all operations return sentinels (`-1`/`false`/`0.0`) on error.

use lynxer_abi::{export_float, export_int, clynxer_module};
use rodio::{Decoder, OutputStream, OutputStreamHandle, Sink, Source};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::sync::Mutex;

// --- Handle registry -------------------------------------------------------

struct SoundEntry {
    path: String,
    // Volume last requested through `setSoundVolume`; applied to new sinks.
    volume: f32,
    // Active player for this handle, if any. Dropping a rodio `Sink` stops
    // playback, so the sink has to outlive the call that started it.
    sink: Option<Sink>,
}

struct SoundState {
    entries: Vec<Option<SoundEntry>>,
    // The output stream is kept alive for the whole process; the handle is
    // what creates sinks. `None` when no audio device could be opened, in
    // which case playback ops report "did not start" rather than failing.
    output: Option<(OutputStream, OutputStreamHandle)>,
}

impl SoundState {
    fn new() -> Self {
        SoundState {
            entries: Vec::new(),
            output: OutputStream::try_default().ok(),
        }
    }
}

// Thread-local state so the interpreter can hold one SoundState per thread.
thread_local! {
    static STATE: Mutex<Option<SoundState>> = Mutex::new(None);
}

fn with_state<F: FnOnce(&mut SoundState) -> R, R>(f: F) -> R {
    STATE.with(|cell| {
        let mut guard = cell.lock().unwrap();
        if guard.is_none() {
            *guard = Some(SoundState::new());
        }
        f(guard.as_mut().unwrap())
    })
}

// --- Ops -------------------------------------------------------------------

// Opens `idx` and plays it, replacing any player already running for the
// handle. Returns 1 on success, 0 when the handle, the file, or the audio
// device is unusable.
fn start_playback(state: &mut SoundState, idx: usize, looping: bool) -> i64 {
    let SoundState { entries, output } = state;
    let handle = match output.as_ref() {
        Some((_, handle)) => handle,
        None => return 0,
    };
    let entry = match entries.get_mut(idx).and_then(|slot| slot.as_mut()) {
        Some(entry) => entry,
        None => return 0,
    };
    let file = match File::open(Path::new(&entry.path)) {
        Ok(file) => file,
        Err(_) => return 0,
    };
    let source = match Decoder::new(BufReader::new(file)) {
        Ok(source) => source,
        Err(_) => return 0,
    };
    if let Some(previous) = entry.sink.take() {
        previous.stop();
    }
    let sink = match Sink::try_new(handle) {
        Ok(sink) => sink,
        Err(_) => return 0,
    };
    sink.set_volume(entry.volume);
    if looping {
        sink.append(source.repeat_infinite());
    } else {
        sink.append(source);
    }
    sink.play();
    entry.sink = Some(sink);
    1
}

// Load an audio file. Returns a stable handle (index), or -1 on failure.
export_int!(sound_load, args, {
    let path = args.string(0).to_string();
    with_state(|state| {
        // Validate that the file can be opened and decoded before registering.
        let file = match File::open(Path::new(&path)) {
            Ok(file) => file,
            Err(_) => return -1,
        };
        if Decoder::new(BufReader::new(file)).is_err() {
            return -1;
        }
        let idx = state.entries.len();
        state.entries.push(Some(SoundEntry {
            path,
            volume: 1.0,
            sink: None,
        }));
        idx as i64
    })
});

// Load a streaming audio file. Returns a handle, or -1 on failure.
//
// rodio decodes from the file handle either way, so this is the same
// operation as `load`; the two entry points mirror the Python contract.
export_int!(sound_load_streaming, args, {
    let path = args.string(0).to_string();
    with_state(|state| {
        let file = match File::open(Path::new(&path)) {
            Ok(file) => file,
            Err(_) => return -1,
        };
        if Decoder::new(BufReader::new(file)).is_err() {
            return -1;
        }
        let idx = state.entries.len();
        state.entries.push(Some(SoundEntry {
            path,
            volume: 1.0,
            sink: None,
        }));
        idx as i64
    })
});

// Play a loaded sound once. Returns true when playback starts.
export_int!(sound_play, args, {
    let idx = args.int(0) as usize;
    with_state(|state| start_playback(state, idx, false))
});

// Start looping playback. Returns true when playback starts.
export_int!(sound_loop, args, {
    let idx = args.int(0) as usize;
    with_state(|state| start_playback(state, idx, true))
});

// Stop playback for a sound. Returns true when the handle was valid.
export_int!(sound_stop, args, {
    let idx = args.int(0) as usize;
    with_state(|state| match state.entries.get_mut(idx).and_then(|slot| slot.as_mut()) {
        Some(entry) => {
            if let Some(sink) = entry.sink.take() {
                sink.stop();
            }
            1
        }
        None => 0,
    })
});

// Pause playback. Returns true only when a player is active.
export_int!(sound_pause, args, {
    let idx = args.int(0) as usize;
    with_state(|state| match state.entries.get(idx).and_then(|slot| slot.as_ref()) {
        Some(entry) => match &entry.sink {
            Some(sink) => {
                sink.pause();
                1
            }
            None => 0,
        },
        None => 0,
    })
});

// Resume playback. Returns true only when a player is active.
export_int!(sound_resume, args, {
    let idx = args.int(0) as usize;
    with_state(|state| match state.entries.get(idx).and_then(|slot| slot.as_ref()) {
        Some(entry) => match &entry.sink {
            Some(sink) => {
                sink.play();
                1
            }
            None => 0,
        },
        None => 0,
    })
});

// Set volume (0.0-1.0, clamped). Returns true when the handle was valid.
export_int!(sound_set_volume, args, {
    let idx = args.int(0) as usize;
    let level = args.float(1).clamp(0.0, 1.0);
    with_state(|state| match state.entries.get_mut(idx).and_then(|slot| slot.as_mut()) {
        Some(entry) => {
            entry.volume = level;
            if let Some(sink) = &entry.sink {
                sink.set_volume(level);
            }
            1
        }
        None => 0,
    })
});

// Return whether the handle currently has audio playing.
export_int!(sound_is_playing, args, {
    let idx = args.int(0) as usize;
    with_state(|state| match state.entries.get(idx).and_then(|slot| slot.as_ref()) {
        Some(entry) => match &entry.sink {
            // `empty()` stays true once the queued sources are drained or
            // stopped, and a paused sink is not playing either.
            Some(sink) => {
                if sink.empty() || sink.is_paused() {
                    0
                } else {
                    1
                }
            }
            None => 0,
        },
        None => 0,
    })
});

// Return the decoded duration in seconds, or 0.0 for an invalid handle.
export_float!(sound_length, args, {
    let idx = args.int(0) as usize;
    with_state(|state| {
        let entry = match state.entries.get(idx).and_then(|slot| slot.as_ref()) {
            Some(entry) => entry,
            None => return 0.0,
        };
        let file = match File::open(Path::new(&entry.path)) {
            Ok(file) => file,
            Err(_) => return 0.0,
        };
        let decoder = match Decoder::new(BufReader::new(file)) {
            Ok(decoder) => decoder,
            Err(_) => return 0.0,
        };
        decoder.total_duration().map_or(0.0, |d| d.as_secs_f64())
    })
});

// Release a loaded sound and invalidate its handle. Returns true on success.
export_int!(sound_release, args, {
    let idx = args.int(0) as usize;
    with_state(|state| match state.entries.get_mut(idx) {
        Some(slot) => match slot.take() {
            Some(entry) => {
                if let Some(sink) = entry.sink {
                    sink.stop();
                }
                1
            }
            // Releasing an already-released handle reports failure, matching
            // the reference implementation.
            None => 0,
        },
        None => 0,
    })
});

// Return the number of currently loaded sound handles.
export_int!(sound_count, args, {
    with_state(|state| state.entries.iter().filter(|slot| slot.is_some()).count() as i64)
});

const OPS: &[(&str, &str, &str)] = &[
    ("load", "sound_load", "cdecl:int64(...)"),
    ("loadStreaming", "sound_load_streaming", "cdecl:int64(...)"),
    ("play", "sound_play", "cdecl:int64(...)"),
    ("loop", "sound_loop", "cdecl:int64(...)"),
    ("stop", "sound_stop", "cdecl:int64(...)"),
    ("pause", "sound_pause", "cdecl:int64(...)"),
    ("resume", "sound_resume", "cdecl:int64(...)"),
    ("setVolume", "sound_set_volume", "cdecl:int64(...)"),
    ("isPlaying", "sound_is_playing", "cdecl:int64(...)"),
    ("length", "sound_length", "cdecl:float64(...)"),
    ("release", "sound_release", "cdecl:int64(...)"),
    ("count", "sound_count", "cdecl:int64(...)"),
];

clynxer_module!(OPS);
