# Motorcycle Forensic Data Visualizer (CSV Viewer)

Windows desktop application for viewing and plotting motorcycle datalogging **CSV files** (e.g., from an SD card) after braking experiments.

This app is built with **Vite + React** and packaged as a **Windows installer (NSIS Setup.exe)** using **Electron + electron-builder**.

---

## Features

- Load a `.csv` file (e.g., from SD card)
- View data in a table
- Plot selected signals (multiple series on one chart)
- Export plots as PNG (if enabled in the UI)

---

## CSV Format

The app expects a time-series CSV with a header row. Typical columns:

- `timestamp`
- `speed`
- `front_wheel_speed`
- `rear_wheel_speed`
- `brake_pressure`
- `acceleration`

The app can also work with different column names as long as:
- the first row is a header
- plotted columns contain numeric data

---

## Client Installation (Windows)

### 1) Install
1. Run the provided installer:
   - `Motorcycle Forensic Data Visualizer Setup <version>.exe`
2. Follow the setup wizard.
3. Start the application via:
   - Start Menu, or
   - Desktop shortcut (if enabled during installation)

> Important: Do not run the application from inside a ZIP or copied partial folder.  
> Always install using the Setup.exe installer.

### 2) Use with SD card
1. Insert the SD card into the laptop.
2. Open **Motorcycle Forensic Data Visualizer**.
3. Click **Load CSV** (or the equivalent UI button).
4. Select the CSV file from the SD card.
5. Select signals to plot and export graphs if required.

---

## Common Issues (Client)

### Windows SmartScreen warning
Because the installer may not be code-signed, Windows can show a warning the first time.

- Click **More info → Run anyway** (if permitted by policy).
- If installation is blocked by organization policy, contact IT.

---

## Developer Setup (Windows)

### Requirements
- Node.js (LTS recommended)
- npm

### Install dependencies
From project root:
`npm install`

### Build frontend
`npm run build`

### Packaging (Windows Installer)
> Important: Vite config for Electron
> Electron loads the app via file://..., so Vite must use relative asset paths.
In vite.config.ts, ensure:
`base: "./"`

## Build NSIS Setup.exe
`npx electron-builder --win nsis --x64`

After a successful build, the installer will be located in the configured output directory (commonly):

`release/`

**Expected artifacts:**
- `Motorcycle Forensic Data Visualizer Setup <version>.exe` (send this to the client)
- `release/win-unpacked/` (debug/portable folder output)

# This application is developed using Google AI Studio
<div align="center">
<img width="1200" height="475" alt="GHBanner" src="https://github.com/user-attachments/assets/0aa67016-6eaf-458a-adb2-6e31a0763ed6" />
</div>
