# random

Random number helpers and utilities (wraps Python's `random`).

Functions:

- `seed(n)` — seed RNG (pass `0` to use system entropy).
- `random()` — float in `[0.0, 1.0)`.
- `randint(a,b)` — integer in `[a,b]`.
- `uniform(a,b)` — float in `[a,b]`.
- `randrange(start, stop)`, `randrangeStep(start, stop, step)` — ranged integer choices.
- `gauss(mu, sigma)` — Gaussian float.
- `coinflip()` — boolean equally likely.
- `sampleInt(items)` / `sampleStr(items)` — pick from pipe-separated list.
- `shuffle(items)` — shuffle pipe-separated list.
- `triangular(lo,hi,mid)` — triangular distribution.
- `uuid4()` — random UUID4 string.
- `randHex(n)` — `n` hex characters.
- `choice(items)`, `sample(items,k)`, `randBool()` — selection helpers.

Notes:
- Many helpers accept or return pipe-separated strings for lists; parse accordingly.