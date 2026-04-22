# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Organic Maps is a privacy-first offline maps & GPS app for hiking, cycling, and driving. Built on OpenStreetMap data with native C++ core and platform-specific UIs for Android (Java/Kotlin), iOS (Objective-C/Swift), and desktop (Qt).

## Build Commands

### Initial Setup
```bash
git clone --recurse-submodules --shallow-submodules https://github.com/organicmaps/organicmaps.git
cd organicmaps
bash ./configure.sh
```

### Desktop (Linux/macOS)
```bash
# Release build
tools/unix/build_omim.sh -r desktop

# Debug build
tools/unix/build_omim.sh -d desktop

# Run desktop app
../omim-build-release/OMaps        # Linux
../omim-build-release/OMaps.app/Contents/MacOS/OMaps  # macOS
```

### Android
```bash
cd android
./gradlew assembleFdroidDebug      # Debug APK
./gradlew runFdroidDebug           # Build and run
./gradlew assembleFdroidBeta       # Beta APK

# Build options
./gradlew -Parm64 runGoogleDebug   # Specific arch (arm64/arm32/x64/x86)
./gradlew -Ppch assembleBeta       # With precompiled headers (15% faster)
```

### iOS
Open `xcode/omim.xcworkspace` in Xcode, select "OMaps" scheme, build and run.

## Testing

### Run All Unit Tests
```bash
cmake . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target all
cd build && ctest -L "omim-test" --output-on-failure
```

### Run Specific Tests
```bash
ctest -R "base_tests|coding_tests" --output-on-failure
ctest -L "omim-test" -E "search_tests" --output-on-failure  # Exclude pattern
```

### Test Coverage
```bash
cmake . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS=-g1 -DCOVERAGE_REPORT=ON
# Build and run tests, then:
cd build && cmake --build . --target omim_coverage
# Report in build/coverage_report/
```

## Code Style

- **C++/Java/ObjC**: Use `clang-format` 21.0+ (config in `.clang-format`)
  ```bash
  tools/unix/clang-format.sh           # Format all
  git clang-format                      # Format staged changes
  git clang-format HEAD~1              # Format last commit
  ```
- **Swift**: Use `swiftformat`
- **C++ Standard**: C++23
- Functions: aim for <60 lines, file should fit comfortably on screen

## Architecture

### Core C++ Libraries (`libs/`)
- `base/` - Logging, caches, macros, fundamental utilities
- `coding/` - I/O, serialization, data processing
- `geometry/` - Geometric primitives and algorithms
- `indexer/` - Map file processing, classificator, feature types
- `search/` - Search ranking and query processing
- `routing/` - Navigation and routing engine
- `map/` - Application business logic, scene management
- `drape/` - Graphics rendering library core
- `drape_frontend/` - Scene and resource management for Drape
- `platform/` - Platform abstraction (files, HTTP, location)
- `storage/` - Map file storage and updates
- `editor/` - OSM data editing functionality

### Platform UIs
- `android/` - Android app (Java/Kotlin)
- `iphone/` - iOS app (Objective-C/Swift)
- `qt/` - Desktop app (Qt/C++)

### Map Generation
- `generator/` - Map building tools from OSM data
- `data/` - Map data, styles, borders, classifications

### Key Data Files (`data/`)
- `classificator.txt` - Hierarchical list of map feature types
- `mapcss-mapping.csv` - OSM tags → OM types mapping
- `drules_proto*` - Drawing rules (generated from styles)
- `strings/` - UI translations

## Commit Message Format

```
[subsystem] Short description in imperative mood

Longer explanation of what and why (not how).
Wrap at 72 characters.

Fixes: #123
```

Subsystem prefixes: `[android]`, `[ios]`, `[qt]`, `[routing]`, `[search]`, `[generator]`, `[indexer]`, `[drape]`, `[platform]`, `[storage]`, `[3party]`, `[docs]`

## Debug Commands

Enter in search box to toggle features:
- `?light`/`?dark` - Theme switching
- `?isolines`/`?no-isolines` - Elevation contours
- `?debug-info` - Show FPS, zoom, renderer info
- `?debug-rect` - Show icon/label collision boxes
- `?gl`/`?vulkan`/`?metal` - Force renderer (requires restart)
- `?edits` - Show local OSM edits

## PR Requirements

- Small PRs (~100 lines ideal, 1000 max)
- Sign-off required: `git commit -s -m "message"`
- Include testing info in PR description
- Auto-generated files (strings, styles) in separate commits
- If using LLM tools, mention in PR description
