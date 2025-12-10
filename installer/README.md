# OpenPLC Windows Installer

This directory contains files for creating a single Windows installer (.exe) for OpenPLC Runtime that clients can run to install and set up OpenPLC automatically.

## Overview

The installer provides:
- ✅ Automatic installation with no command-line interaction required
- ✅ Python environment setup
- ✅ Runtime compilation (or pre-compiled runtime)
- ✅ Windows Service installation (optional)
- ✅ Desktop shortcuts
- ✅ Automatic startup option

## Prerequisites for Building the Installer

1. **Inno Setup Compiler** (free)
   - Download from: https://jrsoftware.org/isdl.php
   - Install Inno Setup 6 or later

2. **Windows Build Machine with MSYS2** (for compiling runtime)
   - Download MSYS2 from: https://www.msys2.org/
   - Required to compile the C++ runtime and utilities

3. **Python 3.8+** (for building, not required in installer if bundled)

## Building the Installer

### Step 1: Prepare Runtime Files

On a Windows machine with MSYS2 installed:

```batch
cd installer
prepare_installer.bat
```

This script will:
- Compile the OpenPLC runtime (`openplc.exe`)
- Build utilities (`iec2c.exe`, `st_optimizer.exe`, `glue_generator.exe`)
- Collect required DLLs
- Prepare all files in the `runtime/` directory

**Note**: If you don't have MSYS2, you can manually:
1. Compile the runtime on a Windows machine with MSYS2
2. Copy the compiled files to `installer/runtime/`

### Step 2: Compile the Installer

1. Open **Inno Setup Compiler**
2. Open the file: `installer/OpenPLC_Installer.iss`
3. Click **Build > Compile** (or press F9)
4. The installer will be created in: `installer/dist/OpenPLC_Installer.exe`

## Installer Features

### What Gets Installed

- OpenPLC webserver and all Python files
- Pre-compiled runtime executable (if available)
- All static files, scripts, and configuration
- Python virtual environment (created during installation)
- Windows Service (optional, if selected)

### Installation Process

1. **Welcome Screen** - Introduction
2. **License Agreement** - Shows OpenPLC license
3. **Select Destination** - Choose installation directory (default: `C:\Program Files\OpenPLC Runtime`)
4. **Select Components** - Choose what to install
5. **Additional Tasks**:
   - Create desktop icon
   - Create Start Menu shortcuts
   - Start OpenPLC on Windows startup
   - Install as Windows Service (recommended)
6. **Installation** - Automatic setup:
   - Copy files
   - Set up Python environment
   - Install Python dependencies
   - Compile runtime (if not pre-compiled)
   - Install Windows Service (if selected)
7. **Finish** - Option to launch OpenPLC

### Post-Installation

After installation, OpenPLC will be:
- Installed in the selected directory
- Accessible via web interface at `http://localhost:8080`
- Running as a Windows Service (if selected)
- Available via Start Menu and Desktop shortcuts

## Customization

### Modify Installer Appearance

Edit `OpenPLC_Installer.iss`:
- Change `MyAppName`, `MyAppVersion`, `MyAppPublisher`
- Add custom images/logos
- Modify installation paths
- Add custom installation steps

### Bundle Python

To bundle Python with the installer (so clients don't need Python installed):

1. Download Python Embedded: https://www.python.org/downloads/windows/
2. Extract to `installer/python/`
3. The installer will use bundled Python if system Python is not found

### Bundle NSSM for Service

To include NSSM (Non-Sucking Service Manager) for better service management:

1. Download NSSM: https://nssm.cc/download
2. Extract `nssm.exe` to `installer/tools/nssm.exe`
3. The installer will use it automatically

## Distribution

The final installer file (`OpenPLC_Installer.exe`) is a single, self-contained executable that:
- Can be distributed to clients
- Requires no additional files
- Works on Windows 7/8/10/11 (64-bit)
- Requires Administrator privileges for installation

## Troubleshooting

### Runtime Not Compiled

If the runtime isn't pre-compiled:
- The installer will attempt compilation during installation
- Requires MSYS2 to be installed on the client machine (not ideal)
- **Solution**: Always pre-compile the runtime before building the installer

### Python Not Found

If Python is not found during installation:
- Installer will use bundled Python (if included)
- Or prompt user to install Python
- **Solution**: Bundle Python Embedded with the installer

### Service Installation Fails

If Windows Service installation fails:
- Ensure running installer as Administrator
- Check if service name conflicts
- Verify NSSM is available (if using)
- **Solution**: Service installation is optional; OpenPLC can run without it

## File Structure

```
installer/
├── OpenPLC_Installer.iss    # Inno Setup script
├── install_runtime.bat       # Post-installation setup script
├── setup_service.bat         # Windows Service installation
├── start_openplc.bat         # Start script for shortcuts
├── prepare_installer.bat     # Pre-build script
├── runtime/                  # Pre-compiled files (created by prepare_installer.bat)
│   ├── openplc.exe
│   ├── iec2c.exe
│   ├── st_optimizer.exe
│   ├── glue_generator.exe
│   └── *.dll
├── python/                   # Python Embedded (optional)
└── tools/                    # Additional tools (NSSM, etc.)
    └── nssm.exe
```

## Support

For issues or questions:
- OpenPLC Project: https://openplcproject.com/
- GitHub: https://github.com/thiagoralves/OpenPLC_v3
