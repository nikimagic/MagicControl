# AMD SDK headers

Official SDK headers are included locally so SensorProbe can be built from the
portable folder without downloading dependencies. The upstream files are unmodified.

| Directory | Upstream | Pinned revision |
|---|---|---|
| `ADL/include` | [AMD Display Library](https://github.com/GPUOpen-LibrariesAndSDKs/display-library) | `84c881cb2136f04287eb3af12de00a9e2867aa9e` |
| `ADLX/include` | [AMD Device Library eXtra 1.5](https://github.com/GPUOpen-LibrariesAndSDKs/ADLX/tree/d9f04a9bba022d6cf6333f005dd540b4ad19fb63) | `d9f04a9bba022d6cf6333f005dd540b4ad19fb63` |

`sources.json` records the original paths, sizes and SHA-256 hashes of the included
files. ADLX is pinned to SDK **1.5.0.124**, matching the collector's current API
binding. Updating to another SDK major version requires reviewing compatibility.

## Used by SensorProbe

- ADL: official `AdapterInfo`, `ADLPMLogDataOutput`, sensor constants and allocation callback.
- ADLX: official initialization typedefs, typed interfaces, interface IDs and smart pointers.
- `src/AmdWindows.cpp` calls named SDK methods; it does not address ADLX vtables by slot number.
- The source passports include the SDK revisions separately from the installed driver's version.

Driver DLLs come from the installed Windows AMD driver. They are not bundled here.
AMD CPU MSR/SMN and RAM access continue to use the signed modules in `runtime/PawnIO`.
Linux collects AMD data from native kernel interfaces.

## Licenses

- ADL headers retain their original MIT notices; the extracted notice is also in
  `ADL/LICENSE.MIT`. The upstream `ADL SDK EULA.pdf` is preserved alongside them.
- ADLX: `ADLX/ADLX SDK License Agreement.pdf`, with the notices in each header retained.

Only monitoring calls are used. The included SDKs also declare tuning functions;
their presence in headers does not enable tuning in this utility.
