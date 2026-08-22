# Low-level TODO

## Native execution & FFI

- [ ] Native function calling from an address (currently working on... partially done)
- [ ] Function-address values
- [ ] C/C++ FFI
- [ ] Dynamic library loading and symbol lookup
- [ ] Native callbacks
- [ ] ABI and calling-convention support

## Native memory

- [x] C++-backed raw allocation and typed memory
- [x] Native struct allocation and field access
- [x] Alignment and layout introspection
- [x] Explicit native byte-order operations
- [ ] Atomic memory operations
- [ ] Volatile memory operations
- [ ] Memory protection
- [ ] Safe native handles with ownership and lifetime checks

## Native concurrency

- [ ] Native thread primitives

## Runtime / portability

- [ ] Native error/status values instead of silent failures
- [ ] Portable native-extension builds