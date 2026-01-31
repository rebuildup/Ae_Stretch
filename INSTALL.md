# Installation Guide

This guide will help you install the stretch_v2 plugin for Adobe After Effects on Windows and macOS.

## Prerequisites

- Adobe After Effects CC or later
- Administrator privileges (for system-wide installation)

## Installation Locations

After Effects plugins can be installed in two locations:

### 1. User-Specific Location (Recommended)
Plugins installed here are only available to the current user.

**Windows:**
```
C:\Users\<YourUsername>\AppData\Roaming\Adobe\After Effects\<Version>\Plug-ins\
```

**macOS:**
```
~/Library/Application Support/Adobe/After Effects/<Version>/Plug-ins/
```

### 2. System-Wide Location
Plugins installed here are available to all users on the computer.

**Windows:**
```
C:\Program Files\Adobe\Adobe After Effects <Version>\Support Files\Plug-ins\
```

**macOS:**
```
/Library/Application Support/Adobe/After Effects/<Version>/Plug-ins/
```

## Installation Steps

### Windows

1. **Build the Plugin** (if not already built)
   - Open `Win/Stretch.sln` in Visual Studio 2022 or later
   - Select the desired platform (x64 or ARM64)
   - Select Release configuration
   - Build the solution
   - Find the compiled `stretch_v2.aex` file in the output directory

2. **Create Plugin Directory**
   - Navigate to your chosen installation location
   - Create a new folder named `stretch_v2` if it doesn't exist

3. **Install Plugin File**
   - Copy `stretch_v2.aex` to the plugin directory
   - For example: `C:\Users\<YourUsername>\AppData\Roaming\Adobe\After Effects\2024\Plug-ins\stretch_v2\`

4. **Restart After Effects**
   - If After Effects is running, close and restart it
   - The plugin should appear in the Effects & Presets panel under "361do_plugins" category

### macOS

1. **Build the Plugin** (if not already built)
   - Open `Mac/Stretch.xcodeproj` in Xcode
   - Select the desired scheme (My Mac - Intel or My Mac - Apple Silicon)
   - Select Release configuration
   - Build the project
   - Find the compiled `stretch_v2.plugin` bundle in the Products folder

2. **Create Plugin Directory**
   - Navigate to your chosen installation location in Finder
   - Create a new folder named `stretch_v2` if it doesn't exist

3. **Install Plugin Bundle**
   - Copy `stretch_v2.plugin` to the plugin directory
   - For example: `~/Library/Application Support/Adobe/After Effects/2024/Plug-ins/stretch_v2/`

4. **Restart After Effects**
   - If After Effects is running, close and restart it
   - The plugin should appear in the Effects & Presets panel under "361do_plugins" category

## Verifying Installation

1. Open Adobe After Effects
2. Create a new composition or open an existing project
3. Go to **Effect > 361do_plugins** or search in the Effects & Presets panel
4. Look for "stretch_v2" in the list

## Troubleshooting

### Plugin not appearing

- **Check After Effects version compatibility**: Ensure you're using After Effects CC or later
- **Verify installation location**: Make sure the plugin is in the correct Plug-ins folder
- **Check architecture compatibility**:
  - Windows: Ensure the plugin architecture (x64/ARM64) matches your After Effects installation
  - macOS: Ensure the plugin architecture (Intel/ARM) matches your After Effects installation
- **Restart After Effects**: Some changes require a full restart

### Plugin crashes or errors

- **Check for multiple versions**: Remove any old versions of the plugin
- **Verify file integrity**: Re-download or rebuild the plugin
- **Check system requirements**: Ensure your system meets the minimum requirements
- **Consult logs**: Check After Effects crash logs for detailed error information

## Uninstallation

### Windows
1. Navigate to the installation directory
2. Delete the `stretch_v2` folder or `stretch_v2.aex` file
3. Restart After Effects

### macOS
1. Navigate to the installation directory
2. Delete the `stretch_v2.plugin` bundle
3. Restart After Effects

## Support

If you encounter any issues during installation:
- Support URL: https://x.com/361do_sleep
- Category: 361do_plugins
