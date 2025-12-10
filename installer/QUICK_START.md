# Quick Start: Creating OpenPLC Windows Installer

## For Distribution to Clients

You want to create a **single .exe installer** that clients can double-click to install OpenPLC with zero command-line interaction.

## Steps to Create the Installer

### 1. Install Inno Setup (Free)
- Download: https://jrsoftware.org/isdl.php
- Install Inno Setup 6 or later

### 2. Prepare Runtime Files (One-Time Setup)

On a Windows machine with MSYS2:

```batch
cd installer
prepare_installer.bat
```

This compiles the runtime and prepares all files.

**Alternative**: If you already have compiled files, just copy them to `installer/runtime/`:
- `openplc.exe`
- `iec2c.exe`
- `st_optimizer.exe`
- `glue_generator.exe`
- Any required `.dll` files

### 3. Build the Installer

1. Open **Inno Setup Compiler**
2. File → Open → Select `installer/OpenPLC_Installer.iss`
3. Build → Compile (or press F9)
4. Done! Installer is in `installer/dist/OpenPLC_Installer.exe`

### 4. Distribute

Give clients the single file: **`OpenPLC_Installer.exe`**

They just:
1. Double-click the installer
2. Follow the wizard
3. OpenPLC is installed and running!

## What Clients Get

After installation:
- ✅ OpenPLC fully installed and configured
- ✅ Web interface at `http://localhost:8080`
- ✅ Windows Service (if they selected it)
- ✅ Desktop/Start Menu shortcuts
- ✅ No command-line interaction needed

## Optional: Bundle Python

If you want clients to not need Python installed:

1. Download Python Embedded: https://www.python.org/downloads/windows/
2. Extract to `installer/python/`
3. Rebuild installer

The installer will automatically use bundled Python if system Python isn't found.

## Troubleshooting

**Q: Runtime compilation fails during installation?**
A: Pre-compile the runtime using `prepare_installer.bat` before building the installer.

**Q: Clients need MSYS2?**
A: No! Only you (the builder) need MSYS2. Clients just run the installer.

**Q: Can I customize the installer?**
A: Yes! Edit `OpenPLC_Installer.iss` - it's well-documented.

## Full Documentation

See `installer/README.md` for detailed information.
