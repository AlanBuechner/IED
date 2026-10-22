# semver

A library and a command-line tool for [Semantic Versioning 2.0.0](https://semver.org): versions, their precedence,
and ranges of versions.

```
semver compare <a> <b>               prints -1, 0 or 1 as a has lower, the same or higher precedence than b
semver satisfies <version> <range>   prints yes or no
semver sort <file>                   prints the file's versions (one per line, blank lines skipped) by ascending precedence
```

An invalid version or range prints `error: invalid version <text>` or `error: invalid range <text>` to standard error
and exits with code 1 (`error: cannot read <file>` for a file that cannot be read); wrong arguments print the usage
and exit with code 2. `data/versions.txt` is an example for `sort`.

## Versions

`MAJOR.MINOR.PATCH`, then optionally `-` and pre-release identifiers, then optionally `+` and build metadata
identifiers: `1.0.0`, `1.0.0-alpha.1`, `1.0.0+20130313144700`, `1.0.0-rc.1+build.5`.

- The three numbers are non-negative integers without leading zeros (`0` is fine, `01` is not) that fit in 64 bits.
- Identifiers are separated by dots, are not empty, and use only `[0-9A-Za-z-]`.
- A numeric pre-release identifier has no leading zeros (`alpha.01` is invalid, `0a` is fine); build identifiers may
  have them (`+001`).

## Precedence (semver.org section 11)

1. Major, minor and patch are compared numerically, in that order.
2. With the same numbers, a version with a pre-release has lower precedence than one without: `1.0.0-rc.1 < 1.0.0`.
3. Two pre-releases are compared identifier by identifier: numeric ones numerically, alphanumeric ones in ASCII order,
   and a numeric identifier is lower than an alphanumeric one. When all of the shorter list's identifiers are equal,
   the shorter list is lower.
4. Build metadata does not count: `1.0.0+a` and `1.0.0+b` have the same precedence.

`1.0.0-alpha < 1.0.0-alpha.1 < 1.0.0-alpha.beta < 1.0.0-beta < 1.0.0-beta.2 < 1.0.0-beta.11 < 1.0.0-rc.1 < 1.0.0`.
`sort` keeps versions of the same precedence in the file's order.

## Ranges

Comparators separated by spaces, all of which must hold: `=V` (or a bare `V`), `>V`, `>=V`, `<V`, `<=V`, and

- `^V`: changes that do not modify the left-most non-zero number. `^1.2.3` is `>=1.2.3 <2.0.0`, `^0.2.3` is
  `>=0.2.3 <0.3.0`, `^0.0.3` is `>=0.0.3 <0.0.4`.
- `~V`: patch changes. `~1.2.3` is `>=1.2.3 <1.3.0`.

`>=1.0.0 <2.0.0` is every 1.x.y version. A version with a pre-release satisfies a range only when one of the range's
comparators has a version with the same major, minor and patch and a pre-release too (npm's rule): `1.2.3-beta.2`
satisfies `>=1.2.3-beta.1`, but `1.3.0-beta` does not satisfy `>=1.2.0`.
