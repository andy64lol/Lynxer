# TODO

## Native execution & FFI

- [x] Native function calling from an address
- [x] Function-address values
- [ ] C/C++ FFI
- [ ] Dynamic library loading and symbol lookup
- [ ] Native callbacks
- [ ] ABI and calling-convention support

## Native memory

- [x] C++-backed raw allocation and typed memory
- [x] Native struct allocation and field access
- [x] Alignment and layout introspection
- [x] Explicit native byte-order operations
- [x] Atomic memory operations
- [x] Volatile memory operations
- [x] Memory protection
- [x] Safe native handles with ownership and lifetime checks

## Native concurrency

- [ ] Native thread primitives

## Runtime / portability

- [ ] Native error/status values instead of silent failures
- [ ] Portable native-extension builds

## Bundling

- [x] --bundle command allowing .lynx files turn into a bundled x86-64 Linux ELF executeable with the lynxer interpreter inside bundled
- [x] Make sure --bundle command works
- [x] Make --bundle generate: build/ and dist/
- [x] Make Lynxer to bundle the compiled bytecode stored in build/bytecode
- [ ] Test if bundled projects works