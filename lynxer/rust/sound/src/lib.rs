//! Lynxer `sound` stdlib backend: audio loading and playback.
//!
//! Replaces the Python Arcade backend with Rust `rodio`/`cpal`.
//! The Lynxer-facing contract matches `lynxer/stdlib/sound.lynx`:
//! handles are integer indices into a module-local registry,
//! and all operations return sentinels (`-1`/`false`/`0.0`) on error.

use lynxer_abi::{export_float, export_int, lynxer_module};
use rodio::buffer::SamplesBuffer;
use rodio::{Decoder, OutputStream, OutputStreamHandle, Sink, Source};
use std::fs::File;
use std::io::BufReader;
use std::path::Path;
use std::sync::Mutex;

/// Audio decoded once, at load time, for a *static* load.
struct Decoded {
    channels: u16,
    sample_rate: u32,
    samples: Vec<f32>,
    /// The duration the container reported, when it reports one. Preferred over
    /// the sample count so the value is what the previous implementation
    /// computed.
    reported: Option<f32>,
}

impl Decoded {
    /// The decoded length in seconds.
    fn seconds(&self) -> f32 {
        if let Some(reported) = self.reported {
            return reported;
        }
        let channels = self.channels.max(1) as usize;
        if self.sample_rate == 0 {
            return 0.0;
        }
        (self.samples.len() / channels) as f32 / self.sample_rate as f32
    }
}

// --- Handle registry -------------------------------------------------------

struct SoundEntry {
    path: String,
    // Volume last requested through `setSoundVolume`; applied to new sinks.
    volume: f32,
    // Active player for this handle, if any. Dropping a rodio `Sink` stops
    // playback, so the sink has to outlive the call that started it.
    sink: Option<Sink>,
    // Decoded length in seconds, resolved when the handle was created so
    // `getSoundLength` never opens the file again.
    length: f32,
    // A *static* load keeps the decoded samples, so playback no longer needs
    // the file; a *streaming* load leaves this `None` and decodes on play.
    decoded: Option<Decoded>,
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

/// Fully decodes `path` into memory. `None` when the file cannot be opened or
/// decoded.
fn decode_file(path: &str) -> Option<Decoded> {
    let file = File::open(Path::new(path)).ok()?;
    let decoder = Decoder::new(BufReader::new(file)).ok()?;
    let channels = decoder.channels();
    let sample_rate = decoder.sample_rate();
    let reported = decoder.total_duration().map(|d| d.as_secs_f32());
    let samples: Vec<f32> = decoder.convert_samples().collect();
    Some(Decoded {
        channels,
        sample_rate,
        samples,
        reported,
    })
}

/// Reads only what is needed for the length: a streaming load must not decode
/// the whole file. A container that carries no duration header is decoded
/// exactly once here, and the samples are then dropped.
fn probe_length(path: &str) -> Option<f32> {
    let file = File::open(Path::new(path)).ok()?;
    let decoder = Decoder::new(BufReader::new(file)).ok()?;
    match decoder.total_duration() {
        Some(duration) => Some(duration.as_secs_f32()),
        None => decode_file(path).map(|decoded| decoded.seconds()),
    }
}

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
    if let Some(previous) = entry.sink.take() {
        previous.stop();
    }
    let sink = match Sink::try_new(handle) {
        Ok(sink) => sink,
        Err(_) => return 0,
    };
    sink.set_volume(entry.volume);
    match entry.decoded.as_ref() {
        // A static load plays the samples it already holds, even if the file
        // has been moved or deleted since.
        Some(decoded) => {
            let buffer = SamplesBuffer::new(
                decoded.channels,
                decoded.sample_rate,
                decoded.samples.clone(),
            );
            if looping {
                sink.append(buffer.repeat_infinite());
            } else {
                sink.append(buffer);
            }
        }
        // A streaming load decodes as it plays, so it needs the file.
        None => {
            let file = match File::open(Path::new(&entry.path)) {
                Ok(file) => file,
                Err(_) => return 0,
            };
            let source = match Decoder::new(BufReader::new(file)) {
                Ok(source) => source.convert_samples::<f32>(),
                Err(_) => return 0,
            };
            if looping {
                sink.append(source.repeat_infinite());
            } else {
                sink.append(source);
            }
        }
    }
    sink.play();
    entry.sink = Some(sink);
    1
}

/// Registers a handle for `path`, or `-1` when it cannot be read. A static load
/// (`static_load`) keeps the decoded samples; a streaming load keeps only the
/// path. Either way the length is resolved **now**, so `getSoundLength` never
/// opens the file again.
fn load_entry(path: &str, static_load: bool) -> i64 {
    with_state(|state| {
        let (length, decoded) = if static_load {
            let decoded = match decode_file(path) {
                Some(decoded) => decoded,
                None => return -1,
            };
            (decoded.seconds(), Some(decoded))
        } else {
            match probe_length(path) {
                Some(length) => (length, None),
                None => return -1,
            }
        };
        let idx = state.entries.len();
        state.entries.push(Some(SoundEntry {
            path: path.to_string(),
            volume: 1.0,
            sink: None,
            length,
            decoded,
        }));
        idx as i64
    })
}

/// The cached length of a handle, or `0.0` when it is not live.
fn entry_length(idx: usize) -> f32 {
    with_state(|state| {
        state
            .entries
            .get(idx)
            .and_then(|slot| slot.as_ref())
            .map(|entry| entry.length)
            .unwrap_or(0.0)
    })
}

// Load an audio file **into memory**. Returns a stable handle (index), or -1
// on failure. The file is decoded here, so playback does not need it again.
export_int!(sound_load, args, {
    load_entry(args.string(0), true)
});

// Load a **streaming** audio file: only the header is read now, and the file is
// decoded as it plays. Returns a handle, or -1 on failure.
export_int!(sound_load_streaming, args, {
    load_entry(args.string(0), false)
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

// Return the decoded duration in seconds, or 0.0 for an invalid handle. The
// length was resolved at load time, so this never touches the file.
export_float!(sound_length, args, {
    entry_length(args.int(0) as usize) as f64
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

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;
    use std::path::PathBuf;

    /// A minimal 8-bit mono PCM WAV with `samples` samples at `sample_rate`, so
    /// the decoded length is exactly `samples / sample_rate` seconds.
    fn write_wav(path: &PathBuf, sample_rate: u32, samples: u32) {
        let channels: u16 = 1;
        let bits: u16 = 8;
        let block_align = channels * (bits / 8);
        let byte_rate = sample_rate * block_align as u32;
        let mut out = Vec::new();
        out.extend_from_slice(b"RIFF");
        out.extend_from_slice(&(36 + samples).to_le_bytes());
        out.extend_from_slice(b"WAVE");
        out.extend_from_slice(b"fmt ");
        out.extend_from_slice(&16u32.to_le_bytes());
        out.extend_from_slice(&1u16.to_le_bytes()); // PCM
        out.extend_from_slice(&channels.to_le_bytes());
        out.extend_from_slice(&sample_rate.to_le_bytes());
        out.extend_from_slice(&byte_rate.to_le_bytes());
        out.extend_from_slice(&block_align.to_le_bytes());
        out.extend_from_slice(&bits.to_le_bytes());
        out.extend_from_slice(b"data");
        out.extend_from_slice(&samples.to_le_bytes());
        out.extend(std::iter::repeat(128u8).take(samples as usize));
        std::fs::write(path, out).expect("the scratch WAV is writable");
    }

    fn scratch(name: &str) -> PathBuf {
        let dir = std::env::temp_dir().join("lynxer_sound_tests");
        std::fs::create_dir_all(&dir).expect("the scratch directory is creatable");
        dir.join(name)
    }

    // The point of caching the length: `getSoundLength` no longer depends on the
    // file still being there. A static load decodes it up front; a streaming
    // load reads the header and leaves the samples alone.
    #[test]
    fn the_length_is_cached_at_load_time() {
        for static_load in [true, false] {
            let path = scratch(if static_load { "static.wav" } else { "stream.wav" });
            write_wav(&path, 8000, 8000); // exactly one second
            let handle = load_entry(path.to_str().unwrap(), static_load);
            assert!(handle >= 0, "static_load={static_load} should load");
            let idx = handle as usize;
            assert!(
                (entry_length(idx) - 1.0).abs() < 0.01,
                "static_load={static_load} length={}",
                entry_length(idx)
            );
            std::fs::remove_file(&path).expect("the scratch WAV is removable");
            assert!(
                (entry_length(idx) - 1.0).abs() < 0.01,
                "static_load={static_load} length after removal={}",
                entry_length(idx)
            );
        }
    }

    #[test]
    fn a_static_load_keeps_the_decoded_samples() {
        let path = scratch("kept.wav");
        write_wav(&path, 8000, 4000); // half a second
        let handle = load_entry(path.to_str().unwrap(), true);
        let idx = handle as usize;
        let samples = with_state(|state| {
            state.entries[idx]
                .as_ref()
                .and_then(|entry| entry.decoded.as_ref())
                .map(|decoded| decoded.samples.len())
        });
        assert_eq!(samples, Some(4000));
        assert!((entry_length(idx) - 0.5).abs() < 0.01);
    }

    #[test]
    fn a_file_that_is_not_audio_does_not_load() {
        let path = scratch("nonsense.bin");
        std::fs::write(&path, b"this is not an audio file").unwrap();
        assert_eq!(load_entry(path.to_str().unwrap(), true), -1);
        assert_eq!(load_entry(path.to_str().unwrap(), false), -1);
        assert_eq!(
            load_entry("/definitely/not/here.wav", true),
            -1,
            "a missing file is a failure, not a silent success"
        );
    }
}
