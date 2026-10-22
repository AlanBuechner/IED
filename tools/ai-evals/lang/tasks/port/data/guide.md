# Tidewater: a field guide for contributors

Tidewater is a small **tide-table service**: it reads harbor gauge readings, predicts the next high and
low water for each station, and serves the tables over HTTP. This guide is for people who want to change
it. If you only want to *run* it, read [the operator notes](docs/operators.md) instead.

> **Note:** the service is in maintenance mode. We take bug fixes and small features; large rewrites need
> an issue first, so that nobody spends a weekend on something we cannot merge.

## Contents

1. [Getting the code](#getting-the-code)
2. [Building and testing](#building-and-testing)
3. [How a prediction is made](#how-a-prediction-is-made)
4. [Style](#style)
5. [Sending a change](#sending-a-change)

---

## Getting the code

Clone the repository and fetch the sample gauge data (about 40 MB):

```sh
git clone https://example.org/tidewater.git
cd tidewater
./tools/fetch-samples.sh --stations=north,harbor-mouth
```

The samples land in `data/samples/`. They are *not* checked in: the files are large, and the harbor
authority asks that copies stay out of public mirrors.

### Requirements

- A C++20 compiler (clang 17 or later, or MSVC 19.38)
- CMake 3.28 or later
- Python 3.12 for the tools under `tools/`
  - `numpy` for the plotting scripts
  - `requests` for `fetch-samples.sh`
- About 2 GB of free disk space

## Building and testing

The usual CMake dance works:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Every pull request runs the same steps in CI, plus `clang-tidy` with the checks listed in
`.clang-tidy`. A warning is a failure: please fix it rather than silencing it with `// NOLINT`.

> Tip: `ctest -R harmonic` runs only the harmonic-analysis tests, which is handy when you are
> working on `src/predict/`.
>
> ```sh
> ctest --test-dir build -R harmonic -j 8
> ```

### Test data

Tests read fixtures from `tests/fixtures/`. Each fixture is a CSV file with a header line:

```csv
station,timestamp,level_cm
north,2024-03-01T00:00:00Z,212
north,2024-03-01T00:06:00Z,215
```

Keep fixtures **small** (a few hundred rows) and *deterministic*. If a test needs a long series,
generate it in the test with `make_series(station, start, count)` rather than checking in a file.

## How a prediction is made

A prediction goes through three stages:

1. **Cleaning.** Readings outside the gauge's range are dropped, and gaps shorter than 30 minutes
   are filled by linear interpolation.
2. **Fitting.** The cleaned series is fitted to the station's constituents (M2, S2, N2, K1, O1 and
   friends) by least squares. See `fit_constituents()` in `src/predict/fit.cpp`.
3. **Extrapolating.** The fitted constituents are summed forward in time, and the turning points of
   the sum become the high and low waters.
   - Turning points closer than 2 hours are merged.
   - A turning point less than 5 cm from its neighbor is ignored.

The constants live in `src/predict/constants.h`; change them only with a test that shows why.

### Why least squares?

It is *boring*, and boring is good here. Fancier fits (Kalman filters, neural nets) were tried in
2021 and gave predictions that were better on average but **much** worse on the days that matter:
storm surges. The discussion is in [issue 112](https://example.org/tidewater/issues/112).

## Style

We follow the [house style](https://example.org/style) with a few local rules:

* Names say what a thing *is*: `high_water_time`, not `hwt` or `t2`.
* Functions do one thing. If a function needs a comment that says "first ... then ...", split it.
* No global mutable state. The station table is passed in, not reached for.
* Errors are values: return `std::expected<T, Error>`; throw only for programming errors.
* Units go in names when they are not obvious: `level_cm`, `interval_s`.

A few things are **not** style questions and are checked by CI:

- formatting (`clang-format`, config in `.clang-format`)
- includes sorted and minimal (`include-what-you-use`)
- no `using namespace` in headers

> A good rule of thumb from a former maintainer:
>
> > If the reviewer has to ask what it does, the name is wrong.

## Sending a change

1. Open an issue, unless the change is a small fix.
2. Make a branch from `main` named after the issue: `fix/112-surge-offset`.
3. Write the test first when you can. A bug fix **must** come with a test that fails without it.
4. Keep commits focused; squash fixups before asking for review.
5. Open the pull request and fill in the template:
   - what changed and why
   - how you tested it
   - anything the reviewer should look at first

Reviews usually take a day or two. If nobody has looked after a week, ping `@tidewater/maintainers`
in the pull request.

### Changing the HTTP API

The API is used by the harbor's display boards, which are updated *twice a year*. So:

- never remove a field; add a new one and mark the old one deprecated
- never change a field's unit
- new endpoints go under `/v2/`

An example response, for reference:

```json
{
  "station": "north",
  "next": [
    { "kind": "high", "time": "2024-03-01T05:42:00Z", "level_cm": 381 },
    { "kind": "low", "time": "2024-03-01T11:57:00Z", "level_cm": 64 }
  ]
}
```

## Troubleshooting

**The build fails with `error: 'expected' file not found`.** Your compiler is too old, or it is
using an old standard library. On Linux, install `libstdc++-14-dev`; on macOS, use the Xcode 16
toolchain.

**Predictions are off by exactly one hour.** The station's time zone is set instead of UTC. Every
time in Tidewater is UTC; the display boards convert. See `docs/time.md`.

**`fetch-samples.sh` says "403 Forbidden".** The harbor authority rotates its download keys each
quarter. Ask in the chat for the current one, and *never* commit it.

---

Thanks for helping! Questions go to the chat, bugs to the [issue tracker](https://example.org/tidewater/issues),
and anything about the harbor data itself to the authority's contact, `data@example.org` &mdash; not to us.
