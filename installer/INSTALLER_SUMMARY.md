# OpenPLC Windows Installer - Complete Solution

## What You Get

A **single .exe installer file** (`OpenPLC_Installer.exe`) that your clients can:
- ✅ Double-click to install
- ✅ No command-line interaction required
- ✅ Automatic setup of everything
- ✅ OpenPLC running immediately after installation

## Solution Overview

This installer solution uses **Inno Setup** (free, industry-standard Windows installer) to create a professional installer that:

1. **Installs all files** - Copies OpenPLC webserver, runtime, and all dependencies
2. **Sets up Python** - Creates virtual environment and installs all Python packages
3. **Compiles runtime** - Uses pre-compiled runtime (recommended) or compiles during install
4. **Installs Windows Service** - Optional, runs OpenPLC as a background service
5. **Creates shortcuts** - Desktop and Start Menu shortcuts for easy access

## File Structure

```
installer/
├── OpenPLC_Installer.iss          # Main installer script (Inno Setup)
├── install_runtime.bat             # Post-installation setup script
├── setup_service.bat               # Windows Service installer
├── start_openplc.bat               # Start script for shortcuts
├── prepare_installer.bat          # Pre-build script (compiles runtime)
├── compile_program_windows.bat    # Windows-native compilation script
├── README.md                       # Detailed documentation
├── QUICK_START.md                  # Quick start guide
└── runtime/                        # Pre-compiled files (created by prepare_installer.bat)
    ├── openplc.exe
    ├── iec2c.exe
    ├── st_optimizer.exe
    ├── glue_generator.exe
    └── *.dll
```

## Workflow

### For You (Builder):

1. **One-time setup**: Run `prepare_installer.bat` on Windows with MSYS2
   - This compiles the runtime and all utilities
   - Creates pre-compiled files in `runtime/` directory

2. **Build installer**: Open `OpenPLC_Installer.iss` in Inno Setup and compile
   - Creates `dist/OpenPLC_Installer.exe`

3. **Distribute**: Give clients the single `OpenPLC_Installer.exe` file

### For Your Clients:

1. **Double-click** `OpenPLC_Installer.exe`
2. **Follow wizard** (click Next, Accept, Install)
3. **Done!** OpenPLC is installed and running
4. **Access** web interface at `http://localhost:8080`

## Key Features

### Automatic Installation
- No manual steps required
- All dependencies handled automatically
- Python environment created automatically
- Runtime compiled or uses pre-compiled version

### Professional Installer
- Modern wizard interface
- License agreement display
- Customizable installation options
- Uninstaller included

### Windows Service Support
- Optional Windows Service installation
- Runs OpenPLC in background
- Auto-starts on Windows boot
- Professional service management

### Flexible Python Setup
- Uses system Python if available
- Falls back to bundled Python (if included)
- Creates isolated virtual environment
- Installs all required packages

## Requirements

### To Build the Installer:
- Windows machine
- MSYS2 (for compiling runtime)
- Inno Setup Compiler (free)
- Python 3.8+ (for building)

### For Clients:
- Windows 7/8/10/11 (64-bit)
- Administrator privileges (for installation)
- Python 3.8+ (optional if bundled)

## Installation Options

The installer provides these options during installation:

1. **Desktop Icon** - Create desktop shortcut
2. **Start Menu** - Add to Start Menu
3. **Auto-start** - Start OpenPLC on Windows boot
4. **Windows Service** - Install as background service (recommended)

## Post-Installation

After installation, clients get:

- **Installation directory**: `C:\Program Files\OpenPLC Runtime\`
- **Web interface**: `http://localhost:8080`
- **Service**: Running in background (if selected)
- **Shortcuts**: Desktop and Start Menu entries
- **Uninstaller**: Available in Windows Settings

## Customization

You can customize:

- **App name/version**: Edit `OpenPLC_Installer.iss`
- **Installation path**: Change `DefaultDirName`
- **Bundled Python**: Add Python Embedded to `python/` directory
- **Service name**: Modify `setup_service.bat`
- **Branding**: Add custom images/icons

## Next Steps

1. Read `QUICK_START.md` for step-by-step instructions
2. Read `README.md` for detailed documentation
3. Run `prepare_installer.bat` to compile runtime
4. Build installer using Inno Setup
5. Test the installer on a clean Windows machine
6. Distribute to clients!

## Support

- **Inno Setup**: https://jrsoftware.org/isinfo.php
- **OpenPLC**: https://openplcproject.com/
- **Documentation**: See `README.md` in this directory
