//! Lynxer `crypto` stdlib backend: hashes, HMAC, constant-time comparison, OS
//! randomness and Ed25519 signatures.
//!
//! Binary payloads cross the module ABI as text. A digest or signature is
//! returned as lower-case hex / base64, and a caller that already holds bytes
//! as base64 passes them to the matching `hashBase64` op; arbitrary file
//! contents go through `hashFile` / `hmacFile`, which read the bytes here.
//!
//! Failures are scalar sentinels: an unknown algorithm, a malformed payload or
//! an I/O error yields `""` (or `false` for a predicate).

use base64::engine::general_purpose::STANDARD;
use base64::Engine as _;
use digest::Digest;
use ed25519_dalek::{Signature, Signer, SigningKey, Verifier, VerifyingKey};
use hmac::{Hmac, Mac};
use lynxer_abi::{export_int, export_string, lynxer_module};
use serde::Serialize;
use subtle::ConstantTimeEq;

/// The largest payload the random helpers will allocate, so a bad count cannot
/// exhaust memory.
const MAX_RANDOM_BYTES: i64 = 1 << 20;

fn to_base64(bytes: &[u8]) -> String {
    STANDARD.encode(bytes)
}

fn from_base64(text: &str) -> Option<Vec<u8>> {
    STANDARD.decode(text).ok()
}

/// A digest of `data`, as lower-case hex, or `None` for an unknown algorithm.
fn hash_bytes(algorithm: &str, data: &[u8]) -> Option<Vec<u8>> {
    Some(match algorithm {
        "sha1" => sha1::Sha1::digest(data).to_vec(),
        "sha224" => sha2::Sha224::digest(data).to_vec(),
        "sha256" => sha2::Sha256::digest(data).to_vec(),
        "sha384" => sha2::Sha384::digest(data).to_vec(),
        "sha512" => sha2::Sha512::digest(data).to_vec(),
        "md5" => md5::Md5::digest(data).to_vec(),
        "sha3-256" => sha3::Sha3_256::digest(data).to_vec(),
        "sha3-512" => sha3::Sha3_512::digest(data).to_vec(),
        "blake3" => blake3::hash(data).as_bytes().to_vec(),
        _ => return None,
    })
}

macro_rules! hmac_of {
    ($digest:ty, $key:expr, $data:expr) => {{
        let mut mac = <Hmac<$digest>>::new_from_slice($key).ok()?;
        mac.update($data);
        mac.finalize().into_bytes().to_vec()
    }};
}

/// An HMAC of `data` under `key`, or `None` for an algorithm without one.
fn hmac_bytes(algorithm: &str, key: &[u8], data: &[u8]) -> Option<Vec<u8>> {
    Some(match algorithm {
        "sha1" => hmac_of!(sha1::Sha1, key, data),
        "sha224" => hmac_of!(sha2::Sha224, key, data),
        "sha256" => hmac_of!(sha2::Sha256, key, data),
        "sha384" => hmac_of!(sha2::Sha384, key, data),
        "sha512" => hmac_of!(sha2::Sha512, key, data),
        "sha3-256" => hmac_of!(sha3::Sha3_256, key, data),
        "sha3-512" => hmac_of!(sha3::Sha3_512, key, data),
        _ => return None,
    })
}

fn random_bytes(count: i64) -> Option<Vec<u8>> {
    if !(0..=MAX_RANDOM_BYTES).contains(&count) {
        return None;
    }
    let mut buffer = vec![0u8; count as usize];
    getrandom::getrandom(&mut buffer).ok()?;
    Some(buffer)
}

#[derive(Serialize)]
struct KeyPair {
    private: String,
    public: String,
}

fn ed25519_keypair() -> Option<(Vec<u8>, Vec<u8>)> {
    let mut secret = [0u8; 32];
    getrandom::getrandom(&mut secret).ok()?;
    let signing = SigningKey::from_bytes(&secret);
    Some((secret.to_vec(), signing.verifying_key().to_bytes().to_vec()))
}

fn ed25519_sign(private_base64: &str, message: &str) -> Option<Vec<u8>> {
    let secret: [u8; 32] = from_base64(private_base64)?.try_into().ok()?;
    let signing = SigningKey::from_bytes(&secret);
    Some(signing.sign(message.as_bytes()).to_bytes().to_vec())
}

fn ed25519_verify(public_base64: &str, message: &str, signature_base64: &str) -> bool {
    (|| -> Option<bool> {
        let public: [u8; 32] = from_base64(public_base64)?.try_into().ok()?;
        let verifying = VerifyingKey::from_bytes(&public).ok()?;
        let signature = Signature::from_slice(&from_base64(signature_base64)?).ok()?;
        Some(verifying.verify(message.as_bytes(), &signature).is_ok())
    })()
    .unwrap_or(false)
}

// --- ops --------------------------------------------------------------------

export_string!(crypto_hash, args, {
    match hash_bytes(args.string(0), args.string(1).as_bytes()) {
        Some(digest) => hex::encode(digest),
        None => String::new(),
    }
});

export_string!(crypto_hash_file, args, {
    match std::fs::read(args.string(1)) {
        Ok(bytes) => match hash_bytes(args.string(0), &bytes) {
            Some(digest) => hex::encode(digest),
            None => String::new(),
        },
        Err(_) => String::new(),
    }
});

export_string!(crypto_hash_base64, args, {
    match from_base64(args.string(1)) {
        Some(bytes) => match hash_bytes(args.string(0), &bytes) {
            Some(digest) => hex::encode(digest),
            None => String::new(),
        },
        None => String::new(),
    }
});

export_string!(crypto_hmac, args, {
    match hmac_bytes(
        args.string(0),
        args.string(1).as_bytes(),
        args.string(2).as_bytes(),
    ) {
        Some(mac) => hex::encode(mac),
        None => String::new(),
    }
});

export_string!(crypto_hmac_file, args, {
    match std::fs::read(args.string(2)) {
        Ok(bytes) => match hmac_bytes(args.string(0), args.string(1).as_bytes(), &bytes) {
            Some(mac) => hex::encode(mac),
            None => String::new(),
        },
        Err(_) => String::new(),
    }
});

export_int!(crypto_verify_hmac, args, {
    let expected = match hex::decode(args.string(3)) {
        Ok(bytes) => bytes,
        Err(_) => return 0,
    };
    match hmac_bytes(
        args.string(0),
        args.string(1).as_bytes(),
        args.string(2).as_bytes(),
    ) {
        Some(mac) => i64::from(mac.ct_eq(&expected).unwrap_u8()),
        None => 0,
    }
});

export_int!(crypto_constant_time_equals, args, {
    i64::from(
        args.string(0)
            .as_bytes()
            .ct_eq(args.string(1).as_bytes())
            .unwrap_u8(),
    )
});

export_string!(crypto_random_bytes, args, {
    match random_bytes(args.int(0)) {
        Some(bytes) => to_base64(&bytes),
        None => String::new(),
    }
});

export_string!(crypto_random_hex, args, {
    match random_bytes(args.int(0)) {
        Some(bytes) => hex::encode(bytes),
        None => String::new(),
    }
});

export_string!(crypto_random_token, args, {
    match random_bytes(args.int(0)) {
        Some(bytes) => base64::engine::general_purpose::URL_SAFE_NO_PAD.encode(bytes),
        None => String::new(),
    }
});

export_string!(crypto_generate_ed25519_keypair, args, {
    let _ = args;
    match ed25519_keypair() {
        Some((secret, public)) => serde_json::to_string(&KeyPair {
            private: to_base64(&secret),
            public: to_base64(&public),
        })
        .unwrap_or_default(),
        None => String::new(),
    }
});

export_string!(crypto_sign_ed25519, args, {
    match ed25519_sign(args.string(0), args.string(1)) {
        Some(signature) => to_base64(&signature),
        None => String::new(),
    }
});

export_int!(crypto_verify_ed25519, args, {
    ed25519_verify(args.string(0), args.string(1), args.string(2)) as i64
});

const OPS: &[(&str, &str, &str)] = &[
    ("hash", "crypto_hash", "cdecl:cstring(...)"),
    ("hashFile", "crypto_hash_file", "cdecl:cstring(...)"),
    ("hashBase64", "crypto_hash_base64", "cdecl:cstring(...)"),
    ("hmac", "crypto_hmac", "cdecl:cstring(...)"),
    ("hmacFile", "crypto_hmac_file", "cdecl:cstring(...)"),
    ("verifyHmac", "crypto_verify_hmac", "cdecl:int64(...)"),
    (
        "constantTimeEquals",
        "crypto_constant_time_equals",
        "cdecl:int64(...)",
    ),
    ("randomBytes", "crypto_random_bytes", "cdecl:cstring(...)"),
    ("randomHex", "crypto_random_hex", "cdecl:cstring(...)"),
    ("randomToken", "crypto_random_token", "cdecl:cstring(...)"),
    (
        "generateEd25519KeyPair",
        "crypto_generate_ed25519_keypair",
        "cdecl:cstring(...)",
    ),
    ("signEd25519", "crypto_sign_ed25519", "cdecl:cstring(...)"),
    ("verifyEd25519", "crypto_verify_ed25519", "cdecl:int64(...)"),
];

lynxer_module!(OPS);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sha256_known_answer() {
        assert_eq!(
            hex::encode(hash_bytes("sha256", b"abc").unwrap()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
        );
    }

    #[test]
    fn hmac_known_answer() {
        let mac = hmac_bytes(
            "sha256",
            b"key",
            b"The quick brown fox jumps over the lazy dog",
        )
        .unwrap();
        assert_eq!(
            hex::encode(mac),
            "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8"
        );
    }

    #[test]
    fn ed25519_round_trip() {
        let (secret, public) = ed25519_keypair().unwrap();
        let signature = ed25519_sign(&to_base64(&secret), "hello").unwrap();
        assert!(ed25519_verify(
            &to_base64(&public),
            "hello",
            &to_base64(&signature)
        ));
        assert!(!ed25519_verify(
            &to_base64(&public),
            "bye",
            &to_base64(&signature)
        ));
    }

    #[test]
    fn unknown_algorithm_is_none() {
        assert!(hash_bytes("nope", b"x").is_none());
        assert!(hmac_bytes("nope", b"k", b"x").is_none());
    }
}
