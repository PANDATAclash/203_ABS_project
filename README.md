````markdown
# Developer Update Guide — Motorcycle Forensic Data Visualizer (Python)

This document explains how to update the application after changing `app.py`, rebuild the executable, and create a new Windows installer.

---

## Project Structure

Recommended folder structure:

```text
csv_viewer_py/
├─ app.py
├─ installer.iss
├─ requirements.txt
├─ assets/
│  └─ app_icon.ico
├─ build/
├─ dist/
└─ installer_output/
````

---

## When You Change `app.py`

Any time you change:

* UI layout
* CSV parsing
* plotting behavior
* export functionality
* app icon handling

you must rebuild the application and then rebuild the installer.

---

## Step 1 — Activate the Virtual Environment

If you created a virtual environment, activate it first.

### Windows CMD

```bat
.venv\Scripts\activate
```

### Windows PowerShell

```powershell
.venv\Scripts\Activate.ps1
```

---

## Step 2 — Test the Application Locally

Run the Python app directly before packaging:

```bash
python app.py
```

Check that:

* the window opens
* CSV loading works
* signals plot correctly
* zoom/pan works
* PNG export works

If something is broken here, fix it before building.

---

## Step 3 — Clean Old Build Files (Recommended)

To avoid old files being reused, remove previous build output.

### Windows CMD

```bat
rmdir /s /q build
rmdir /s /q dist
```

If `installer_output` should also be cleared:

```bat
rmdir /s /q installer_output
```

---

## Step 4 — Rebuild the Executable with PyInstaller

Run:

```bash
pyinstaller --noconfirm --windowed --icon=assets/app_icon.ico --name "Motorcycle Forensic Data Visualizer" app.py
```

This creates a new packaged application in:

```text
dist/Motorcycle Forensic Data Visualizer/
```

Important:

* do not send only the `.exe` out of this folder
* the `.exe` depends on other files inside the same folder

---

## Step 5 — Rebuild the Installer with Inno Setup

Open `installer.iss` in **Inno Setup Compiler** and click:

* **Build → Compile**
* or press **F9**

This creates a new installer in:

```text
installer_output/
```

Example output:

```text
installer_output/MotorcycleForensicDataVisualizer-Setup.exe
```

This is the file you send to the client.

---

## Step 6 — Update Version Number Before Releasing

Before creating a new installer, update the version in `installer.iss`.

Find:

```ini
AppVersion=1.0.0
```

Change it to:

```ini
AppVersion=1.0.1
```

Increase this each time you make a new release.

Examples:

* `1.0.0` → first release
* `1.0.1` → small fix
* `1.1.0` → added feature
* `2.0.0` → major redesign

---

## Full Command List

### Activate venv (CMD)

```bat
.venv\Scripts\activate
```

### Run the app locally

```bash
python app.py
```

### Delete old build folders

```bat
rmdir /s /q build
rmdir /s /q dist
rmdir /s /q installer_output
```

### Rebuild executable

```bash
pyinstaller --noconfirm --windowed --icon=assets/app_icon.ico --name "Motorcycle Forensic Data Visualizer" app.py
```

### Optional: reinstall dependencies

```bash
pip install -r requirements.txt
```

### Optional: install PyInstaller if not installed

```bash
pip install pyinstaller
```

---

## Typical Update Workflow

Use this sequence every time:

### 1. Activate environment

```bat
.venv\Scripts\activate
```

### 2. Test current code

```bash
python app.py
```

### 3. Clean old build

```bat
rmdir /s /q build
rmdir /s /q dist
rmdir /s /q installer_output
```

### 4. Rebuild packaged app

```bash
pyinstaller --noconfirm --windowed --icon=assets/app_icon.ico --name "Motorcycle Forensic Data Visualizer" app.py
```

### 5. Open `installer.iss` in Inno Setup

### 6. Update `AppVersion`

### 7. Compile installer

### 8. Send the new installer from:

```text
installer_output/
```

---

## Important Notes

### Do not send only the `.exe` from `dist/`

The executable depends on many other files in the same folder.

Correct:

* create installer with Inno Setup
* send the installer `.exe`

Wrong:

* send only `dist/.../Motorcycle Forensic Data Visualizer.exe`

### Always test before sending

Install the newly built installer on your own machine first and test:

* open app
* load CSV
* plot signals
* export PNG

---

## If the Installer Fails

Check:

* `dist/Motorcycle Forensic Data Visualizer/` exists
* `installer.iss` points to the correct folder
* `assets/app_icon.ico` exists if used in PyInstaller or Inno Setup

Common fix:
Delete old `build/`, `dist/`, and `installer_output/` folders and rebuild from scratch.

---

## Recommended Release Package

Send the client:

* `MotorcycleForensicDataVisualizer-Setup.exe`

Optional extras:

* `UserGuide.pdf`
* sample CSV file

---

## Optional Improvement

To speed up rebuilding later, create a `build.bat` file that runs:

1. clean
2. PyInstaller
3. optionally opens Inno Setup

That makes rebuilding faster and repeatable.

```
```
