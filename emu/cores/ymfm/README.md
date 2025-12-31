# ymfm - Yamaha FM sound cores

This directory contains vendored source files from the ymfm project.

## Source
- **Project**: ymfm (Yamaha FM sound cores)
- **Author**: Aaron Giles
- **Repository**: https://github.com/aaronsgiles/ymfm
- **Commit**: 17decfae857b92ab55fbb30ade2287ace095a381
- **Date**: 2025-12-31

## License
BSD-3-Clause

Copyright (c) 2020-2021 Aaron Giles

See the header comments in each file for full license text.

## Files
- `ymfm.h` - Base classes and core infrastructure
- `ymfm_opz.h` - YM2414 (OPZ) implementation
- `ymfm_fm.h` - FM engine base classes
- `ymfm_fm.ipp` - FM engine template implementations

## Integration
These files are used by `ymfmintf.cpp` to provide YM2414 (OPZ) emulation support in libvgm.
