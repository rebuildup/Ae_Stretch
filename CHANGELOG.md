# Changelog

All notable changes to this project will be documented in this file.

## [1.2.0] - 2025-12-30

### Added
- Multi-threaded row processing support for improved rendering performance
- Fast path for integer coordinate sampling to skip expensive interpolation
- `PF_OutFlag2_REVEALS_ZERO_ALPHA` flag for proper alpha transparency handling
- `PF_OutFlag_I_EXPAND_BUFFER` flag with FrameSetup callback for dynamic output buffer expansion
- `PF_OutFlag2_SUPPORTS_THREAD_RENDERING` flag for thread-safe rendering

### Changed
- Optimized SampleBilinear with pre-computed values and inverse multiplication
- Replaced unchanged area pixel sampling with SampleBilinear function
- Added input image boundary handling and transparent pixel fill
- Refactored buffer expansion calculation to account for angle, anchor point, and downsampling
- Updated AE_Effect_Global_OutFlags_2 for smart rendering support
- Refactored SmartRender function with float type casting and Render function forward declaration
- Changed Render function parameter passing from direct array to pointer array

### Fixed
- Fixed sign logic in forward and backward shift buffer extension calculation
- Fixed black fringing artifacts during anti-aliasing of pixels with transparency by implementing alpha-weighted bilinear sampling
- Fixed row-based optimization optimization for rows entirely before or after the line, now always processes pixel-by-pixel for consistency

### Removed
- Removed FrameSetup function (consolidated into other callbacks)

### Refactor
- Replaced AE sampling suite usage with custom bilinear sampling function
- Changed `sample_params` type from `PF_SampleParams` to `PF_SampPB`

## [1.1.0] - 2025-12-27

### Added
- Initial Smart Render implementation for After Effects
- AE sampling suite integration with sub-pixel sampling
- Custom bilinear sampling function

### Changed
- Replaced custom bilinear sampling with AE's sub-pixel sampling

## [1.0.0] - Initial Release

### Added
- Anchor Point parameter for stretch reference point
- Angle parameter for stretch direction
- Shift Amount parameter (0-10000) for stretch intensity
- Direction parameter (Both/Forward/Backward) for stretch direction
- Basic pixel shifting functionality
